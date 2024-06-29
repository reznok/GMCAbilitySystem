#include "Utility/GameplayElementMapping.h"
#include "GMCAbilityComponent.h"
#include "Misc/DataValidation.h"
#include "Misc/EngineVersionComparison.h"

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
	Reset();
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
#endif

bool FGMCGameplayElementTagPropertyMap::SetValueForMappedProperty(FProperty* Property, float Value)
{
	UObject* Owner = CachedOwner.Get();

	if (const FBoolProperty* BoolProperty = static_cast<FBoolProperty*>(Property))
	{
		BoolProperty->SetPropertyValue_InContainer(Owner, Value > 0.f);
		return true;
	}

	if (const FIntProperty* IntProperty = static_cast<FIntProperty*>(Property))
	{
		IntProperty->SetPropertyValue_InContainer(Owner, FMath::RoundToInt(Value));
		return true;
	}

	if (const FFloatProperty* FloatProperty = static_cast<FFloatProperty*>(Property))
	{
		FloatProperty->SetPropertyValue_InContainer(Owner, Value);
		return true;
	}

	if (const FDoubleProperty* DoubleProperty = static_cast<FDoubleProperty*>(Property))
	{
		DoubleProperty->SetPropertyValue_InContainer(Owner, Value);
		return true;
	}

	return false;	
}

bool FGMCGameplayElementTagPropertyMap::SetValueForMappedProperty(FProperty* Property, FGameplayTagContainer& MatchedTags)
{
	return SetValueForMappedProperty(Property, MatchedTags.Num());
}

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

	const FGameplayAttributeChangedNative::FDelegate AttrDelegate = FGameplayAttributeChangedNative::FDelegate::CreateRaw(this, &FGMCGameplayElementTagPropertyMap::GameplayAttributeChangedCallback);
	AttributeHandle = AbilitySystemComponent->AddAttributeChangeDelegate(AttrDelegate);

	const FGameplayTagFilteredMulticastDelegate::FDelegate TagDelegate = FGameplayTagFilteredMulticastDelegate::FDelegate::CreateRaw(this, &FGMCGameplayElementTagPropertyMap::GameplayTagChangedCallback);
	
	for (int Index = PropertyMappings.Num() - 1; Index >= 0; --Index)
	{
		auto& Mapping = PropertyMappings[Index];
		if (Mapping.TagsToMap.IsValid())
		{
			if (FProperty* Property = OwnerClass->FindPropertyByName(Mapping.PropertyName); Property && IsPropertyTypeValid(Property))
			{
				
				Mapping.PropertyToMap = Property;
				Mapping.DelegateHandle = AbilitySystemComponent->AddFilteredTagChangeDelegate(Mapping.TagsToMap, TagDelegate);
				continue;
			}
		}

		// We're invalid, so remove.
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("FGMCGameplayElementTagPropertyMap: Removing invalid mapping [index %d, tags %s, property %s] for %s"),
			Index, *Mapping.TagsToMap.ToString(), *Mapping.PropertyName.ToString(), *GetNameSafe(Owner));
		PropertyMappings.RemoveAtSwap(Index, 1, false);
	}

	ApplyCurrentTags();
	ApplyCurrentAttributes();
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

void FGMCGameplayElementTagPropertyMap::ApplyCurrentAttributes()
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

	for (auto& Mapping : PropertyMappings)
	{
		FProperty *Property = Mapping.PropertyToMap.Get();
		if (Mapping.TagsToMap.IsValid() && Property && Mapping.TagsToMap.Num() == 1)
		{
			FGameplayTag Attribute = Mapping.TagsToMap.First();
			if (const FAttribute* Attr = AbilityComponent->GetAttributeByTag(Attribute))
			{
				SetValueForMappedProperty(Property, Attr->Value);
			}
		}
	}
}

bool FGMCGameplayElementTagPropertyMap::IsInitialized() const
{
	return CachedAbilityComponent.IsValid();
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

		if (AttributeHandle.IsValid())
		{
			AbilityComponent->RemoveAttributeChangeDelegate(AttributeHandle);
			AttributeHandle.Reset();
		}
	}

	CachedOwner = nullptr;
	CachedAbilityComponent = nullptr;
}

void FGMCGameplayElementTagPropertyMap::Reset()
{
	Unregister();
	AttributeHandle.Reset();
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

#if UE_VERSION_OLDER_THAN(5, 4, 0)
#define GARBAGE_FLAG RF_Garbage
#else
#define GARBAGE_FLAG RF_MirroredGarbage
#endif

void FGMCGameplayElementTagPropertyMap::GameplayAttributeChangedCallback(const FGameplayTag& AttributeTag,
	const float OldValue, const float NewValue)
{
	UObject* Owner = CachedOwner.Get();
	const UGMC_AbilitySystemComponent* AbilityComponent = CachedAbilityComponent.Get();
	
	if (!Owner || !AbilityComponent)
	{
		// Get an object pointer even if it's prepped for garbage collection.
		UObject *TrueOwner = CachedOwner.Get(true);

		// Disable deprecation warnings since we need to use RF_Garbage (deprecated) prior to 5.4 introducing
		// RF_MirroredGarbage.
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (TrueOwner != nullptr && TrueOwner->HasAnyFlags(GARBAGE_FLAG))
		{
			// This happens if our animation blueprint is being used as a child layer; it will be marked for garbage
			// collection, but not deallocated (so the Reset() function hasn't yet been called).
			//
			// In this case, we want to just reset ourselves as though we were being deallocated and bail.
			
			Reset();
			return;
		}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

		// We don't have even a pending-delete object, meaning something has gone VERY wrong.
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("FGMCGameplayElementTagPropertyMap: Received callback on uninitialized map!"));
		return;
	}

	for (auto& Mapping : PropertyMappings)
	{
		if (Mapping.TagsToMap.IsValid() && Mapping.TagsToMap.Num() == 1)
		{
			if (AttributeTag.MatchesTag(Mapping.TagsToMap.First()))
				SetValueForMappedProperty(Mapping.PropertyToMap.Get(), NewValue);
		}
	}
}

bool FGMCGameplayElementTagPropertyMap::IsPropertyTypeValid(const FProperty* Property) const
{
	check(Property);
	return (Property->IsA<FBoolProperty>() || Property->IsA<FIntProperty>() || Property->IsA<FFloatProperty>() || Property->IsA<FDoubleProperty>());
}
