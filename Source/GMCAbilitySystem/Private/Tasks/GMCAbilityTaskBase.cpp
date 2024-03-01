#include "Tasks/GMCAbilityTaskBase.h"
#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

void UGMCAbilityTaskBase::Activate()
{
	Super::Activate();
	RegisterTask(this);
}

void UGMCAbilityTaskBase::SetAbilitySystemComponent(UGMC_AbilityComponent* InAbilitySystemComponent)
{
	this->AbilitySystemComponent = InAbilitySystemComponent;
}

void UGMCAbilityTaskBase::RegisterTask(UGMCAbilityTaskBase* Task)
{
	TaskID = Ability->GetNextTaskID();
	Ability->RegisterTask(TaskID, Task);
}

void UGMCAbilityTaskBase::ClientProgressTask()
{
	FGMCAbilityData ContinueRunning = Ability->InitialAbilityData;
	ContinueRunning.TaskID = TaskID;
	ContinueRunning.bProgressTask = true;
	Ability->AbilityComponent->QueueAbility(ContinueRunning);
}

