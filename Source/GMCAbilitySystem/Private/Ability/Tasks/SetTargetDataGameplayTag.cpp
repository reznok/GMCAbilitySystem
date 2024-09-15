#include "Ability/Tasks/SetTargetDataGameplayTag.h"
#include "GMCAbilityComponent.h"

UGMCAbilityTask_SetTargetDataGameplayTag* UGMCAbilityTask_SetTargetDataGameplayTag::SetTargetDataGameplayTag(UGMCAbility* OwningAbility, FGameplayTag InTag){
	UGMCAbilityTask_SetTargetDataGameplayTag* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataGameplayTag>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = InTag;
	return Task;
}

void UGMCAbilityTask_SetTargetDataGameplayTag::Activate(){
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataGameplayTag::ProgressTask(FInstancedStruct& TaskData){
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataGameplayTag Data = TaskData.Get<FGMCAbilityTaskTargetDataGameplayTag>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataGameplayTag::ClientProgressTask(){
	FGMCAbilityTaskTargetDataGameplayTag TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}


