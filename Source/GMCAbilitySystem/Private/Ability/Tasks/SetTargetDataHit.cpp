// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Tasks/SetTargetDataHit.h"
#include "GMCAbilityComponent.h"

UGMCAbilityTask_SetTargetDataHit* UGMCAbilityTask_SetTargetDataHit::SetTargetDataHit(UGMCAbility* OwningAbility, FHitResult InHit)
{
	UGMCAbilityTask_SetTargetDataHit* Task = NewAbilityTask<UGMCAbilityTask_SetTargetDataHit>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->Target = InHit;
	return Task;
}

void UGMCAbilityTask_SetTargetDataHit::Activate()
{
	Super::Activate();

	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_SetTargetDataHit::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	const FGMCAbilityTaskTargetDataHit Data = TaskData.Get<FGMCAbilityTaskTargetDataHit>();
	
	Completed.Broadcast(Data.Target);
	EndTask();
}

void UGMCAbilityTask_SetTargetDataHit::ClientProgressTask()
{
	FGMCAbilityTaskTargetDataHit TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	TaskData.Target = Target;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}