#pragma once
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "Engine/CancellableAsyncAction.h"
#include "SetTargetDataVector3.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataVector3AsyncActionPin, FVector, Target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataVector3 : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FVector Target{FVector::Zero()};
};

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataVector3 : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataVector3AsyncActionPin	Completed;
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FVector Target;
	
	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Vector3)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataVector3* SetTargetDataVector3(UGMCAbility* OwningAbility, FVector Vector3);
 
	//Overriding BP async action base
	virtual void Activate() override;
};