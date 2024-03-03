#include "Ability/Tasks/GMCAbilityTaskBase.h"

#include "GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"



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
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(FGMCAbilityTaskData{
		.AbilityID = Ability->GetAbilityID(),
		.TaskID = TaskID
	});
	Ability->AbilityComponent->QueueTaskData(TaskDataInstance);
}

