#pragma once
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "Engine/CancellableAsyncAction.h"
#include "SetTargetDataByte.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataByteAsyncActionPin, uint8, Target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataByte : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	uint8 Target{0};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataByte : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataByteAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	uint8 Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Byte)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataByte* SetTargetDataByte(UGMCAbility* OwningAbility, uint8 Byte);
 
	//Overriding BP async action base
	virtual void Activate() override;
};