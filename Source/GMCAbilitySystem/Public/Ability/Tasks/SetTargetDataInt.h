#pragma once
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "Engine/CancellableAsyncAction.h"
#include "SetTargetDataInt.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataIntAsyncActionPin, int, Target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataInt : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	int Target{0};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataInt : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataIntAsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	int Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Int)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataInt* SetTargetDataInt(UGMCAbility* OwningAbility, int Int);
 
	//Overriding BP async action base
	virtual void Activate() override;
};