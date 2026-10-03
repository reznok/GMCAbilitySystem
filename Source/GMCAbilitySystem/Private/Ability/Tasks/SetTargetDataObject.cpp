

#include "Ability/Tasks/SetTargetDataObject.h"

#include "Components/GMCAbilityComponent.h"


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

	if (DrivesPawnLocally())
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataObject::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	if (TaskData.GetScriptStruct() != FGMCAbilityTaskTargetDataObject::StaticStruct())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbilityTask_SetTargetDataObject::ProgressTask: payload is %s, expected FGMCAbilityTaskTargetDataObject; dropped (owner %s)."),
			TaskData.GetScriptStruct() ? *TaskData.GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(Ability ? Ability->GetOwnerActor() : nullptr));
		EndTask();
		return;
	}
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