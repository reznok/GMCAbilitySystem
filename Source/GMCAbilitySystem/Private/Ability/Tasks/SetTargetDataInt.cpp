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

	if (DrivesPawnLocally())
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataInt::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	if (TaskData.GetScriptStruct() != FGMCAbilityTaskTargetDataInt::StaticStruct())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbilityTask_SetTargetDataInt::ProgressTask: payload is %s, expected FGMCAbilityTaskTargetDataInt; dropped (owner %s)."),
			TaskData.GetScriptStruct() ? *TaskData.GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(Ability ? Ability->GetOwnerActor() : nullptr));
		EndTask();
		return;
	}
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
