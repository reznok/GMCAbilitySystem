#include "Ability/Tasks/GMCAbilityTaskBase.h"

#include "GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"



void UGMCAbilityTaskBase::Activate()
{
	Super::Activate();
	RegisterTask(this);
	LastHeartbeatReceivedTime = AbilitySystemComponent->ActionTimer;
}

void UGMCAbilityTaskBase::EndTaskGMAS()
{
	EndTask();
}

void UGMCAbilityTaskBase::SetAbilitySystemComponent(UGMC_AbilitySystemComponent* InAbilitySystemComponent)
{
	this->AbilitySystemComponent = InAbilitySystemComponent;
}

void UGMCAbilityTaskBase::RegisterTask(UGMCAbilityTaskBase* Task)
{
	TaskID = Ability->GetNextTaskID();
	Ability->RegisterTask(TaskID, Task);
}

void UGMCAbilityTaskBase::Tick(float DeltaTime)
{
	// Locally controlled server pawns don't need to send heartbeats
	if (AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn()) return;
	
	// If not the server version of the component, send heartbeats
	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer)
	{
		if (ClientLastHeartbeatSentTime + HeartbeatInterval < AbilitySystemComponent->ActionTimer)
		{
			AbilitySystemComponent->RPCTaskHeartbeat(Ability->GetAbilityID(), TaskID);
			ClientLastHeartbeatSentTime = AbilitySystemComponent->ActionTimer;
		}
	}
	else if (LastHeartbeatReceivedTime + HeartbeatMaxInterval < AbilitySystemComponent->ActionTimer)
	{
		UE_LOG(LogGMCReplication, Error, TEXT("Server Task Heartbeat Timeout, Cancelling Ability: %s"), *Ability->GetName());
		Ability->EndAbility();
	}

}

void UGMCAbilityTaskBase::AncillaryTick(float DeltaTime){
	
}

void UGMCAbilityTaskBase::ClientProgressTask()
{
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
