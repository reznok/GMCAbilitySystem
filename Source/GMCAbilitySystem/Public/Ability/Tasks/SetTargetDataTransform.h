// 

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "SetTargetDataTransform.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataTransformAsyncActionPin, FTransform, Target);


USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataTransform : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FTransform Target{FTransform()};
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataTransform : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataTransformAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FTransform Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Transform)", Category = "GMAS|Abilities|Tasks")
	static UGMCAbilityTask_SetTargetDataTransform* SetTargetDataTransform(UGMCAbility* OwningAbility, FTransform Transform);
 
	//Overriding BP async action base
	virtual void Activate() override;
};
