#include "Ability/Tasks/SetTargetDataInstancedStruct.h"
#include "GMCAbilityComponent.h"

UGMCAbilityTask_SetTargetDataInstancedStruct* UGMCAbilityTask_SetTargetDataInstancedStruct::SetTargetDataInstancedStruct(UGMCAbility* OwningAbility, FInstancedStruct InstancedStruct){
	UGMCAbilityTask_SetTargetDataInstancedStruct* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataInstancedStruct>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = InstancedStruct;
	return Task;
}

void UGMCAbilityTask_SetTargetDataInstancedStruct::Activate(){
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataInstancedStruct::ProgressTask(FInstancedStruct& TaskData){
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataInstancedStruct Data = TaskData.Get<FGMCAbilityTaskTargetDataInstancedStruct>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataInstancedStruct::ClientProgressTask(){
	FGMCAbilityTaskTargetDataInstancedStruct TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}


