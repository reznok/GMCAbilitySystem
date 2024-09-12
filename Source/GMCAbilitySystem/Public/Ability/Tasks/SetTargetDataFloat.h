#pragma once
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "Engine/CancellableAsyncAction.h"
#include "SetTargetDataFloat.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataFloatAsyncActionPin, float, Target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataFloat : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	float Target{0};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataFloat : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataFloatAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	float Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Float)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataFloat* SetTargetDataFloat(UGMCAbility* OwningAbility, float Float);
 
	//Overriding BP async action base
	virtual void Activate() override;
};