#include "Ability/Tasks/SetTargetDataTag.h"
#include "GMCAbilityComponent.h"

UGMCAbilityTask_SetTargetDataTag* UGMCAbilityTask_SetTargetDataTag::SetTargetDataTag(UGMCAbility* OwningAbility, FGameplayTag InTag){
	UGMCAbilityTask_SetTargetDataTag* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataTag>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = InTag;
	return Task;
}

void UGMCAbilityTask_SetTargetDataTag::Activate(){
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataTag::ProgressTask(FInstancedStruct& TaskData){
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataTag Data = TaskData.Get<FGMCAbilityTaskTargetDataTag>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataTag::ClientProgressTask(){
	Super::ClientProgressTask();

	FGMCAbilityTaskTargetDataTag TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}


