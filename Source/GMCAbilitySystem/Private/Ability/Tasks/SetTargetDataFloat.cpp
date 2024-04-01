#include "Ability/Tasks/SetTargetDataFloat.h"

#include "GMCAbilityComponent.h"


UGMCAbilityTask_SetTargetDataFloat* UGMCAbilityTask_SetTargetDataFloat::SetTargetDataFloat(UGMCAbility* OwningAbility,
                                                                                                 float Float)
{
	UGMCAbilityTask_SetTargetDataFloat* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataFloat>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Float;
	return Task;
}

void UGMCAbilityTask_SetTargetDataFloat::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataFloat::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataFloat Data = TaskData.Get<FGMCAbilityTaskTargetDataFloat>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataFloat::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataFloat TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
