// 

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "GMCAbilityMapData.generated.h"

class UGMCAbility;
/**
 * 
 */

USTRUCT(BlueprintType)
struct FAbilityMapData{
	GENERATED_BODY()

	// Input tag: the key abilities are granted and activated by (QueueAbility, GrantAbilityByTag).
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	FGameplayTag InputTag;

	// Ability Objects that the tag should execute
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	TArray<TSubclassOf<UGMCAbility>> Abilities;

	// Whether or not this ability should be automatically granted to the owning Ability Component
	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem")
	bool bGrantedByDefault{true};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityMapData : public UPrimaryDataAsset{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "GMCAbilitySystem", meta=(TitleProperty="{InputTag}"))
	TArray<FAbilityMapData> AbilityMapData;

public:
	const TArray<FAbilityMapData>& GetAbilityMapData() const { return AbilityMapData; }
};
