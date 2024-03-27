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

USTRUCT()
struct FAbilityMapData{
	GENERATED_BODY()

	// Ability Tag
	UPROPERTY(EditDefaultsOnly)
	FGameplayTag InputTag;

	// Ability Objects that the tag should execute
	UPROPERTY(EditDefaultsOnly)
	TArray<TSubclassOf<UGMCAbility>> Abilities;

	// Whether or not this ability should be automatically granted to the owning Ability Component
	UPROPERTY(EditDefaultsOnly)
	bool bGrantedByDefault{true};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityMapData : public UPrimaryDataAsset{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	TArray<FAbilityMapData> AbilityMapData;

public:
	TArray<FAbilityMapData> GetAbilityMapData() {return AbilityMapData;}
};
