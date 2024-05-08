// 

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "SetTargetDataGameplayTag.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataGameplayTagAsyncActionPin, FGameplayTag, Target);


USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataGameplayTag : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTag Target{FGameplayTag()};
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataGameplayTag : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataGameplayTagAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTag Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Gameplay Tag)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataGameplayTag* SetTargetDataGameplayTag(UGMCAbility* OwningAbility, FGameplayTag InTag);
 
	//Overriding BP async action base
	virtual void Activate() override;
};
