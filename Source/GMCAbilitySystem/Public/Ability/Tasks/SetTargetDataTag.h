// 

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "SetTargetDataTag.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataTagAsyncActionPin, FGameplayTag, Target);


USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataTag : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag Target{FGameplayTag()};
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataTag : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataTagAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Gameplay Tag)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataTag* SetTargetDataTag(UGMCAbility* OwningAbility, FGameplayTag InTag);
 
	//Overriding BP async action base
	virtual void Activate() override;
};
