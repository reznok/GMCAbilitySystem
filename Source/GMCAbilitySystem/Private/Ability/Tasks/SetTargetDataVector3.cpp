#include "Ability/Tasks/SetTargetDataVector3.h"

#include "GMCAbilityComponent.h"


UGMCAbilityTask_SetTargetDataVector3* UGMCAbilityTask_SetTargetDataVector3::SetTargetDataVector3(UGMCAbility* OwningAbility,
                                                                                                 FVector Vector3)
{
	UGMCAbilityTask_SetTargetDataVector3* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataVector3>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Vector3;
	return Task;
}

void UGMCAbilityTask_SetTargetDataVector3::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataVector3::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataVector3 Data = TaskData.Get<FGMCAbilityTaskTargetDataVector3>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataVector3::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataVector3 TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
