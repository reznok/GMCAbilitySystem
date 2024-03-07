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
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityMapData : public UPrimaryDataAsset{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly)
	TMap<FGameplayTag, TSubclassOf<UGMCAbility>> AbilityMap;

public:
	TMap<FGameplayTag, TSubclassOf<UGMCAbility>> GetAbilityMap() {return AbilityMap;}
};
