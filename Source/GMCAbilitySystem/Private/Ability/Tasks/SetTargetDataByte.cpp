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

	if (DrivesPawnLocally())
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataByte::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	if (TaskData.GetScriptStruct() != FGMCAbilityTaskTargetDataByte::StaticStruct())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbilityTask_SetTargetDataByte::ProgressTask: payload is %s, expected FGMCAbilityTaskTargetDataByte; dropped (owner %s)."),
			TaskData.GetScriptStruct() ? *TaskData.GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(Ability ? Ability->GetOwnerActor() : nullptr));
		EndTask();
		return;
	}
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
