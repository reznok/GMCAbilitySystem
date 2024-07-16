#include "Ability/Tasks/WaitForGMCMontageChange.h"

#include "GMCOrganicMovementComponent.h"
#include "Components/GMCAbilityComponent.h"



UGMCAbilityTask_WaitForGMCMontageChange::UGMCAbilityTask_WaitForGMCMontageChange(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

UGMCAbilityTask_WaitForGMCMontageChange* UGMCAbilityTask_WaitForGMCMontageChange::WaitForGMCMontageChange(UGMCAbility* OwningAbility)
{
	UGMCAbilityTask_WaitForGMCMontageChange* Task = NewAbilityTask<UGMCAbilityTask_WaitForGMCMontageChange>(OwningAbility);
	return Task;
}

void UGMCAbilityTask_WaitForGMCMontageChange::Activate()
{
	Super::Activate();
	bTickingTask = true;
	
	OrganicMovementCmp = Cast<UGMC_OrganicMovementCmp>(AbilitySystemComponent->GMCMovementComponent);
	
	if (OrganicMovementCmp == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("WaitForGMCMontageChange called without an Organic Movement Component"));
		OnFinish();
		return;
	}

	StartingMontage = OrganicMovementCmp->GetActiveMontage(OrganicMovementCmp->MontageTracker);
	if (StartingMontage == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("WaitForGMCMontageChange called without a running montage"));
		OnFinish();
	}
}

void UGMCAbilityTask_WaitForGMCMontageChange::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	
	const UAnimMontage* RunningMontage = OrganicMovementCmp->GetActiveMontage(OrganicMovementCmp->MontageTracker);
	
	// If the montage has changed, finish the task
	if (StartingMontage != RunningMontage)
	{
		OnFinish();
	}
}

void UGMCAbilityTask_WaitForGMCMontageChange::OnFinish()
{
	if (GetState() != EGameplayTaskState::Finished)
	{
		Completed.Broadcast();
		EndTask();
	}
}
