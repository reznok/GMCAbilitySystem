#include "Tasks/GMCAbilityTaskBase.h"
#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

void UGMCAbilityTaskBase::Activate()
{
	Super::Activate();

	if (OwningAbility == nullptr) return;
	if (OwningAbility->AbilityState == EAbilityState::Ended) Cancel();
	TaskID = OwningAbility->GetNextTaskID();
	OwningAbility->RegisterTask(TaskID, this);
}

void UGMCAbilityTaskBase::InternalTick(float DeltaTime)
{
	if (OwningAbility->AbilityState == EAbilityState::Ended)
	{
		UE_LOG(LogTemp, Warning, TEXT("Task is Ticking after ability has ended. Check Handles and references."));
	}

}

void UGMCAbilityTaskBase::InternalCompleted(bool Forced)
{
	OwningAbility->AbilityComponent->OnFGMCAbilitySystemComponentTickDelegate.Remove(TickHandle);
	if (!Forced) {Completed.Broadcast();}
	Cancel();
	SetReadyToDestroy();
}

void UGMCAbilityTaskBase::InternalClientCompleted()
{	
	FGMCAbilityData ContinueRunning = OwningAbility->InitialAbilityData;
	ContinueRunning.TaskID = TaskID;
	ContinueRunning.bProgressTask = true;
	OwningAbility->AbilityComponent->QueueAbility(ContinueRunning);
	SetReadyToDestroy();
}
