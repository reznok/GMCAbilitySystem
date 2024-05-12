#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GMCAbilityComponent.h"
#include "UObject/Object.h"
#include "GameplayElementMapping.generated.h"

class UGMC_AbilitySystemComponent;
/**
 * Struct used to store an individual mapping between a gameplay tag relevant in some way to GMC and
 * a Blueprint property field. Functionally GMAS' equivalent to GAS's FGameplayTagBlueprintPropertyMap.
 */
USTRUCT()
struct GMCABILITYSYSTEM_API FGMCGameplayElementTagPropertyMapping
{

	GENERATED_BODY()

	FGMCGameplayElementTagPropertyMapping() {}
	FGMCGameplayElementTagPropertyMapping(const FGMCGameplayElementTagPropertyMapping& Other)
	{
		TagsToMap = Other.TagsToMap;
		PropertyToMap = Other.PropertyToMap;
		PropertyName = Other.PropertyName;
		PropertyGuid = Other.PropertyGuid;
	}
	
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	FGameplayTagContainer TagsToMap;

	UPROPERTY(VisibleAnywhere, Category = "GMCAbilitySystem")
	TFieldPath<FProperty> PropertyToMap;	

	UPROPERTY(VisibleAnywhere, Category = "GMCAbilitySystem")
	FName PropertyName;

	UPROPERTY(VisibleAnywhere, Category = "GMCAbilitySystem")
	FGuid PropertyGuid;

	FDelegateHandle DelegateHandle;
	
};


/**
 * Struct containing a collection of individual tag/property mappings. Functionally the
 * GMAS equivalent to GAS' FGameplayTagBlueprintPropertyMap collection.
 */
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMCGameplayElementTagPropertyMap
{
	GENERATED_BODY()

public:

	FGMCGameplayElementTagPropertyMap();
	FGMCGameplayElementTagPropertyMap(const FGMCGameplayElementTagPropertyMap& Other);
	~FGMCGameplayElementTagPropertyMap();

	// Must be called to actually start using this instance.
	void Initialize(UObject* Owner, UGMC_AbilitySystemComponent* AbilitySystemComponent);

	void ApplyCurrentTags();
	void ApplyCurrentAttributes();

	bool IsInitialized() const;

#if WITH_EDITOR
	EDataValidationResult IsDataValid(const UObject* ContainingAsset, FDataValidationContext& Context) const;
#endif
	

protected:

	bool SetValueForMappedProperty(FProperty* Property, float PropValue);
	bool SetValueForMappedProperty(FProperty* Property, FGameplayTagContainer& MatchedTags);
	
	void Unregister();

	void GameplayTagChangedCallback(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags);

	void GameplayAttributeChangedCallback(const FGameplayTag& AttributeTag, const float OldValue, const float NewValue);

	bool IsPropertyTypeValid(const FProperty* Property) const;

	FDelegateHandle AttributeHandle;
	
	TWeakObjectPtr<UObject> CachedOwner;
	TWeakObjectPtr<UGMC_AbilitySystemComponent> CachedAbilityComponent;

	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	TArray<FGMCGameplayElementTagPropertyMapping> PropertyMappings;
};