#include "Ability/Tasks/SetTargetDataTransform.h"
#include "GMCAbilityComponent.h"

UGMCAbilityTask_SetTargetDataTransform* UGMCAbilityTask_SetTargetDataTransform::SetTargetDataTransform(UGMCAbility* OwningAbility, FTransform Transform){
	UGMCAbilityTask_SetTargetDataTransform* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataTransform>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = Transform;
	return Task;
}

void UGMCAbilityTask_SetTargetDataTransform::Activate(){
	Super::Activate();

	if (DrivesPawnLocally())
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataTransform::ProgressTask(FInstancedStruct& TaskData){
	Super::ProgressTask(TaskData);
	if (TaskData.GetScriptStruct() != FGMCAbilityTaskTargetDataTransform::StaticStruct())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbilityTask_SetTargetDataTransform::ProgressTask: payload is %s, expected FGMCAbilityTaskTargetDataTransform; dropped (owner %s)."),
			TaskData.GetScriptStruct() ? *TaskData.GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(Ability ? Ability->GetOwnerActor() : nullptr));
		EndTask();
		return;
	}
	const FGMCAbilityTaskTargetDataTransform Data = TaskData.Get<FGMCAbilityTaskTargetDataTransform>();
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataTransform::ClientProgressTask(){
	FGMCAbilityTaskTargetDataTransform TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}


