#include "Ability/Tasks/GMCAbilityTaskBase.h"

#include "GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"



void UGMCAbilityTaskBase::Activate()
{
	Super::Activate();
	RegisterTask(this);
	LastHeartbeatReceivedTime = AbilitySystemComponent->ActionTimer + HeartbeatMaxInterval;
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

}

void UGMCAbilityTaskBase::AncillaryTick(float DeltaTime){
	// Locally controlled server pawns don't need to send heartbeats
	if (AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn()) return;
	
	// If not the server version of the component, send heartbeats
	if (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer &&
		AbilitySystemComponent->GetNetMode() != NM_ListenServer)
	{
		if (ClientLastHeartbeatSentTime + HeartbeatInterval < AbilitySystemComponent->ActionTimer)
		{
			AbilitySystemComponent->RPCTaskHeartbeat(Ability->GetAbilityID(), TaskID);
			ClientLastHeartbeatSentTime = AbilitySystemComponent->ActionTimer;
		}
	}
	else if (LastHeartbeatReceivedTime + HeartbeatMaxInterval < AbilitySystemComponent->ActionTimer)
	{
		const float TimeSinceLastHeartbeat = AbilitySystemComponent->ActionTimer - LastHeartbeatReceivedTime;
		UE_LOG(LogGMCReplication, Error, TEXT("Server Task Heartbeat Timeout after %.2fs (max: %.2f), Cancelling Ability: %s"),
		  TimeSinceLastHeartbeat, HeartbeatMaxInterval, *Ability->GetName());
		AbilitySystemComponent->OnTaskTimeout.Broadcast(Ability->AbilityTag);
		Ability->EndAbility();
		EndTask();
	}
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

void UGMCAbilityTaskBase::Heartbeat()
{
	AbilitySystemComponent->GMCMovementComponent->SV_SwapServerState();
	LastHeartbeatReceivedTime = AbilitySystemComponent->ActionTimer;
	AbilitySystemComponent->GMCMovementComponent->SV_SwapServerState();
}

bool UGMCAbilityTaskBase::IsClientOrRemoteListenServerPawn() const
{
	return (AbilitySystemComponent->GetNetMode() != NM_DedicatedServer &&
		AbilitySystemComponent->GetNetMode() != NM_ListenServer) ||
		AbilitySystemComponent->GMCMovementComponent->IsLocallyControlledServerPawn();
}
