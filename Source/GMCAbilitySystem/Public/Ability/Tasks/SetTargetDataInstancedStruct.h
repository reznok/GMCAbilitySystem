// 

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "SetTargetDataInstancedStruct.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataInstancedStructAsyncActionPin, FInstancedStruct, Target);


USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataInstancedStruct : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FInstancedStruct Target{FInstancedStruct()};
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataInstancedStruct : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataInstancedStructAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FInstancedStruct Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Instanced Struct)", Category = "GMAS|Abilities|Tasks")
	static UGMCAbilityTask_SetTargetDataInstancedStruct* SetTargetDataInstancedStruct(UGMCAbility* OwningAbility, FInstancedStruct InstancedStruct);
 
	//Overriding BP async action base
	virtual void Activate() override;
};
