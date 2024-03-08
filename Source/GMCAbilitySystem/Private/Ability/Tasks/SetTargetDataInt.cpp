#include "Ability/Tasks/SetTargetDataInt.h"

#include "GMCAbilityComponent.h"


UGMCAbilityTask_SetTargetDataInt* UGMCAbilityTask_SetTargetDataInt::SetTargetDataInt(UGMCAbility* OwningAbility,
                                                                                                 int Int)
{
	UGMCAbilityTask_SetTargetDataInt* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataInt>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Int;
	return Task;
}

void UGMCAbilityTask_SetTargetDataInt::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataInt::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataInt Data = TaskData.Get<FGMCAbilityTaskTargetDataInt>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataInt::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataInt TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
