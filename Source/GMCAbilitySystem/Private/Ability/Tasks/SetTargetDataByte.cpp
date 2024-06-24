#include "Ability/Tasks/SetTargetDataByte.h"

#include "GMCAbilityComponent.h"


UGMCAbilityTask_SetTargetDataByte* UGMCAbilityTask_SetTargetDataByte::SetTargetDataByte(UGMCAbility* OwningAbility,
                                                                                                 uint8 Byte)
{
	UGMCAbilityTask_SetTargetDataByte* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataByte>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Byte;
	return Task;
}

void UGMCAbilityTask_SetTargetDataByte::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataByte::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataByte Data = TaskData.Get<FGMCAbilityTaskTargetDataByte>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataByte::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataByte TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
