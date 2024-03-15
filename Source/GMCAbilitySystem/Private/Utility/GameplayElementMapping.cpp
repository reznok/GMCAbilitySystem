#include "Utility/GameplayElementMapping.h"
#include "GMCAbilityComponent.h"
#include "Misc/DataValidation.h"

FGMCGameplayElementTagPropertyMap::FGMCGameplayElementTagPropertyMap()
{
}

FGMCGameplayElementTagPropertyMap::FGMCGameplayElementTagPropertyMap(const FGMCGameplayElementTagPropertyMap& Other)
{
	ensureMsgf(Other.CachedOwner.IsExplicitlyNull(), TEXT("Tag property maps are invalid in arrays or copy-after-register scenarios."));
	PropertyMappings = Other.PropertyMappings;
}

FGMCGameplayElementTagPropertyMap::~FGMCGameplayElementTagPropertyMap()
{
	Unregister();
}

#if WITH_EDITOR
EDataValidationResult FGMCGameplayElementTagPropertyMap::IsDataValid(const UObject* ContainingAsset,
	FDataValidationContext& Context) const
{
	UClass* OwnerClass = ((ContainingAsset != nullptr) ? ContainingAsset->GetClass() : nullptr);
	if (!OwnerClass)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCGameplayElementTagPropertyMap: called with invalid owner."))
		return EDataValidationResult::Invalid;
	}

	for (const auto& Mapping : PropertyMappings)
	{
		for (auto& Tag : Mapping.TagsToMap)
		{
			if (!Tag.IsValid())
			{
				Context.AddError(FText::Format(FText::FromString("FGMCGameplayElementTagPropertyMap: tag {0} is invalid, but mapped to {1}"),
					FText::FromString(Tag.ToString()), FText::FromName(Mapping.PropertyName) 
					));
			}
		}

		if (const FProperty* OwnerProperty = OwnerClass->FindPropertyByName(Mapping.PropertyName))
		{
			if (!IsPropertyTypeValid(OwnerProperty))
			{
				Context.AddError(FText::Format(FText::FromString("FGMCGameplayElementTagPropertyMap: tag {0} is mapped to property {1}, but property is an unsupported type"),
					FText::FromString(Mapping.TagsToMap.ToString()), FText::FromName(Mapping.PropertyName)));
			}
		}
		else
		{
			Context.AddError(FText::Format(FText::FromString("FGMCGameplayElementTagPropertyMap: tag {0} is mapped to nonexistent property {1}"),
				FText::FromString(Mapping.TagsToMap.ToString()), FText::FromName(Mapping.PropertyName)));
		}
	}

	return (Context.GetNumErrors() > 0 ? EDataValidationResult::Invalid : EDataValidationResult::Valid);
}

bool FGMCGameplayElementTagPropertyMap::SetValueForMappedProperty(FProperty* Property, FGameplayTagContainer& MatchedTags)
{
	UObject* Owner = CachedOwner.Get();

	// We take the matched tags in case we someday want to change this to also support setting an array of matched tags
	// on a property, or something similar.
	const int32 Value = MatchedTags.Num();
	
	if (!Property || !Owner)
	{
		return false;
	}

	if (const FBoolProperty* BoolProperty = Cast<FBoolProperty>(Property))
	{
		BoolProperty->SetPropertyValue_InContainer(Owner, Value > 0);
		return true;
	}

	if (const FIntProperty* IntProperty = Cast<FIntProperty>(Property))
	{
		IntProperty->SetPropertyValue_InContainer(Owner, Value);
		return true;
	}

	if (const FFloatProperty* FloatProperty = Cast<FFloatProperty>(Property))
	{
		FloatProperty->SetPropertyValue_InContainer(Owner, Value);
		return true;
	}

	if (const FDoubleProperty* DoubleProperty = Cast<FDoubleProperty>(Property))
	{
		DoubleProperty->SetPropertyValue_InContainer(Owner, Value);
		return true;
	}

	return false;
}
#endif

void FGMCGameplayElementTagPropertyMap::Initialize(UObject* Owner, UGMC_AbilitySystemComponent* AbilitySystemComponent)
{
	UClass *OwnerClass = (Owner ? Owner->GetClass() : nullptr);
	if (!OwnerClass)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCGameplayElementTagPropertyMap::Initialize() called with invalid owner."));
		return;
	}

	if (!AbilitySystemComponent)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCGameplayElementTagPropertyMap::Initialize() called with invalid ability component"));
		return;
	}

	if ((CachedOwner == Owner) && (CachedAbilityComponent == AbilitySystemComponent))
	{
		// Already initialized for this setup, just exit.
		return;
	}

	if (CachedOwner.IsValid())
	{
		// Changing owners, unregister all current delegates.
		Unregister();
	}

	CachedOwner = Owner;
	CachedAbilityComponent = AbilitySystemComponent;

	FGameplayTagFilteredMulticastDelegate::FDelegate Delegate = FGameplayTagFilteredMulticastDelegate::FDelegate::CreateRaw(this, &FGMCGameplayElementTagPropertyMap::GameplayTagChangedCallback);

	for (int Index = PropertyMappings.Num() - 1; Index >= 0; --Index)
	{
		auto& Mapping = PropertyMappings[Index];
		if (Mapping.TagsToMap.IsValid())
		{
			FProperty* Property = OwnerClass->FindPropertyByName(Mapping.PropertyName);
			if (Property && IsPropertyTypeValid(Property))
			{
				Mapping.PropertyToMap = Property;
				Mapping.DelegateHandle = AbilitySystemComponent->AddFilteredTagChangeDelegate(Mapping.TagsToMap, Delegate);
				continue;
			}
		}

		// We're invalid, so remove.
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCGameplayElementTagPropertyMap: Removing invalid mapping [index %d, tags %s, property %s] for %s"),
			Index, *Mapping.TagsToMap.ToString(), *Mapping.PropertyName.ToString(), *GetNameSafe(Owner));
		PropertyMappings.RemoveAtSwap(Index, 1, false);
	}

	ApplyCurrentTags();
}

void FGMCGameplayElementTagPropertyMap::ApplyCurrentTags()
{
	UObject* Owner = CachedOwner.Get();
	UGMC_AbilitySystemComponent* AbilityComponent = CachedAbilityComponent.Get();

	if (!Owner)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("FGMCGameplayElementTagPropertyMap::ApplyCurrentTags() called with invalid owner."));
		return;
	}

	if (!AbilityComponent)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("FGMCGameplayElementTagPropertyMap::ApplyCurrentTags() called with invalid ability component."));
		return;
	}

	FGameplayTagContainer ActiveTags = AbilityComponent->GetActiveTags();
	
	for (auto& Mapping : PropertyMappings)
	{
		FProperty *Property = Mapping.PropertyToMap.Get();
		if (Mapping.TagsToMap.IsValid() && Property)
		{
			FGameplayTagContainer Matched = ActiveTags.Filter(Mapping.TagsToMap);
			SetValueForMappedProperty(Property, Matched);
		}
	}
}

void FGMCGameplayElementTagPropertyMap::Unregister()
{
	if (UGMC_AbilitySystemComponent* AbilityComponent = CachedAbilityComponent.Get())
	{
		for (auto& Mapping : PropertyMappings)
		{
			if (Mapping.PropertyToMap.Get() && Mapping.DelegateHandle.IsValid())
			{
				AbilityComponent->RemoveFilteredTagChangeDelegate(Mapping.TagsToMap, Mapping.DelegateHandle);
			}
			Mapping.PropertyToMap = nullptr;
			Mapping.DelegateHandle.Reset();
		}
	}

	CachedOwner = nullptr;
	CachedAbilityComponent = nullptr;
}

void FGMCGameplayElementTagPropertyMap::GameplayTagChangedCallback(const FGameplayTagContainer& AddedTags,
	const FGameplayTagContainer& RemovedTags)
{
	UObject* Owner = CachedOwner.Get();
	const UGMC_AbilitySystemComponent* AbilityComponent = CachedAbilityComponent.Get();

	if (!Owner || !AbilityComponent)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("FGMCGameplayElementTagPropertyMap: Received callback on uninitialized map!"));
		return;
	}

	for (auto& Mapping : PropertyMappings)
	{
		const bool bMatched = AddedTags.HasAny(Mapping.TagsToMap) || RemovedTags.HasAny(Mapping.TagsToMap);

		if (bMatched && Mapping.PropertyToMap.Get())
		{
			FGameplayTagContainer Matched = AbilityComponent->GetActiveTags().Filter(Mapping.TagsToMap);
			SetValueForMappedProperty(Mapping.PropertyToMap.Get(), Matched);
		}
	}
}

bool FGMCGameplayElementTagPropertyMap::IsPropertyTypeValid(const FProperty* Property) const
{
	check(Property);
	return (Property->IsA<FBoolProperty>() || Property->IsA<FIntProperty>() || Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>());
}
