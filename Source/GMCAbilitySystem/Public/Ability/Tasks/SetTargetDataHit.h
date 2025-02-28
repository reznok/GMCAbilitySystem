// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityTaskBase.h"
#include "GMCAbilityTaskData.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "SetTargetDataHit.generated.h" 

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataHitAsyncActionPin, FHitResult, target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataHit : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FHitResult Target{FHitResult()};
};


/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataHit : public UGMCAbilityTaskBase
{
	GENERATED_BODY()

public:
	
	UPROPERTY(BlueprintAssignable)
	FUGMCAbilityTaskTargetDataHitAsyncActionPin	Completed;

	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	FHitResult Target;

	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;

	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Hit Result)",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_SetTargetDataHit* SetTargetDataHit(UGMCAbility* OwningAbility, FHitResult InHit);
	
	//Overriding BP async action base
	virtual void Activate() override;
};
