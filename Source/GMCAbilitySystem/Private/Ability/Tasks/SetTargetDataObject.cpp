

#include "Ability/Tasks/SetTargetDataObject.h"


UGMCAbilityTask_SetTargetDataObject* UGMCAbilityTask_SetTargetDataObject::SetTargetDataObject(UGMCAbility* OwningAbility,
																								 UObject* Object)
{
	UGMCAbilityTask_SetTargetDataObject* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataObject>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Object;
	return Task;
}

void UGMCAbilityTask_SetTargetDataObject::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataObject::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataObject Data = TaskData.Get<FGMCAbilityTaskTargetDataObject>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataObject::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataObject TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}