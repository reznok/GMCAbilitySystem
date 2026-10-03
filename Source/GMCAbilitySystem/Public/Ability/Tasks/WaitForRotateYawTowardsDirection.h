#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "GMCMovementUtilityComponent.h"
#include "WaitForRotateYawTowardsDirection.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGMCAbilityTaskWaitForRotateYawTowardsDirectionOutputPin, float, Duration);

class UGMCMovementComponent;

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_RotateYawTowardsDirection : public UGMCAbilityTaskBase
{
	GENERATED_UCLASS_BODY()

	virtual void Activate() override;
	virtual void Tick(float DeltaTime) override;
	
	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitForRotateYawTowardsDirectionOutputPin Completed;

	UFUNCTION(BlueprintCallable, Category = "GMAS|Abilities|Tasks", meta = (HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"))
	static UGMCAbilityTask_RotateYawTowardsDirection* WaitForRotateYawTowardsDirection(UGMCAbility* OwningAbility, FVector TargetDirection, float RotationSpeed);

private:

	UPROPERTY()
	UGMC_MovementUtilityCmp* MovementComponent;

	UPROPERTY()
	FVector DesiredDirection;

	UPROPERTY()
	float RotationSpeed;

	// ActionTimer at activation (move time: identical on client, server and replay).
	double StartTime = 0.0;

	void OnFinish();
};
