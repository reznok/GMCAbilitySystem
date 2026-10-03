#include "Ability/Tasks/WaitForRotateYawTowardsDirection.h"

#include "GMCMovementUtilityComponent.h"
#include "Components/GMCAbilityComponent.h"


UGMCAbilityTask_RotateYawTowardsDirection::UGMCAbilityTask_RotateYawTowardsDirection(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

UGMCAbilityTask_RotateYawTowardsDirection* UGMCAbilityTask_RotateYawTowardsDirection::WaitForRotateYawTowardsDirection(
	UGMCAbility* OwningAbility, FVector TargetDirection, float RotationSpeed)
{
	UGMCAbilityTask_RotateYawTowardsDirection* Task = NewAbilityTask<UGMCAbilityTask_RotateYawTowardsDirection>(OwningAbility);
	Task->DesiredDirection = TargetDirection.GetSafeNormal();
	Task->RotationSpeed = RotationSpeed;
	return Task;
}

void UGMCAbilityTask_RotateYawTowardsDirection::Activate()
{
	Super::Activate();

	MovementComponent = AbilitySystemComponent->GMCMovementComponent;

	if (!MovementComponent)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("RotateYawTowardsDirection called without a valid Movement Component"));
		OnFinish();
		return;
	}

	StartTime = AbilitySystemComponent->ActionTimer;

	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("RotateYawTowardsDirection activated with direction: %s, speed: %f"), 
		*DesiredDirection.ToString(), RotationSpeed);
}

void UGMCAbilityTask_RotateYawTowardsDirection::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!MovementComponent || DesiredDirection.IsNearlyZero())
	{
		OnFinish();
		return;
	}

	MovementComponent->RotateYawTowardsDirection(DesiredDirection, RotationSpeed, DeltaTime);

	// check if rotation is complete
	const FRotator CurrentRotation = MovementComponent->GetOwner()->GetActorRotation();
	const FRotator TargetRotation = DesiredDirection.Rotation();
	const float AngleDifference = FMath::Abs(FRotator::NormalizeAxis(CurrentRotation.Yaw - TargetRotation.Yaw));

	if (AngleDifference < 0.01f)
	{
		OnFinish();
	}
}

void UGMCAbilityTask_RotateYawTowardsDirection::OnFinish()
{
	if (GetState() != EGameplayTaskState::Finished)
	{
		const float Duration = static_cast<float>(AbilitySystemComponent.IsValid() ? AbilitySystemComponent->ActionTimer - StartTime : 0.0);
		Completed.Broadcast(Duration);
		EndTask();
	}
}
