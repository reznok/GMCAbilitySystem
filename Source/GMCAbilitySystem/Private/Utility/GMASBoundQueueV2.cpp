#include "Utility/GMASBoundQueueV2.h"

#include <Utility/GMASBoundQueueV2.h>

#include "GMCAbilitySystem.h"
#include "GMCMovementUtilityComponent.h"


bool FGMASBoundQueueV2::IsValidGMASOperation(const FInstancedStruct& Data) const
{
	// Check if the operation is a valid type
	if (!OperationData.IsValid()) return false;

	const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
	if (!BaseData)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("OperationData is not a valid type"));
		return false;
	}

	return true;
}

bool FGMASBoundQueueV2::IsValidClientOperation(const FInstancedStruct& Data) const
{
	if (Data.IsValid()) 
	{
		const UScriptStruct* StructType = Data.GetScriptStruct();
		if (ValidClientInputOperationTypes.Contains(StructType))
		{
			return true;
		}
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Client operation type %s is not allowed by server as client input"), *StructType->GetName());
	}
	return false;
}

void FGMASBoundQueueV2::ClearStaleOperationData()
{
	const int MaximumFreshMoveIndex = GMCMoveCounter - GMCMovementComponent->MoveHistoryMaxSize;
	for (auto It = OperationDataCacheExpiration.CreateIterator(); It; ++It)
	{
		const int64 MoveAddedAt = It->ModeAddedAt;
		if (MoveAddedAt < MaximumFreshMoveIndex)
		{
			const int OperationID = It->OperationID;
			if (!OperationPayloads.Contains(OperationID))
			{
				UE_LOG(LogGMCAbilitySystem, Warning, TEXT("OperationID %d not found in OperationPayloads, but still in cache expiration map"), OperationID);
				continue;
			}
			// Remove stale operation data
			OperationPayloads.Remove(OperationID);
			It.RemoveCurrent();
		}
	}
}

void FGMASBoundQueueV2::BindToGMC(UGMC_MovementUtilityCmp* MovementComponent)
{
	OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
	
	BI_OperationData = MovementComponent->BindInstancedStruct(
		OperationData,
		EGMC_PredictionMode::ClientAuth_InputOutput,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);
	
	GMCMovementComponent = MovementComponent;
}

void FGMASBoundQueueV2::GenPreLocalMoveExecution()
{
	// Client Logic
	if (GMCMovementComponent->GetNetMode() == NM_Client ||
		GMCMovementComponent->GetNetMode() == NM_Standalone ||
		GMCMovementComponent->IsLocallyControlledListenServerPawn() ||
		GMCMovementComponent->IsLocallyControlledDedicatedServerPawn())
	{
		if (ClientQueuedOperations.Num() == 0)
		{
			OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
			return;
		}

		// Batching is only safe for SERVER-BROADCAST ops (positive IDs from
		// QueueServerOperation/RPCOnServerOperationAdded). Their payloads are
		// cached on BOTH sides; the client only needs to ack the ID and the
		// server resolves the payload from its own cache via
		// ServerProcessAcknowledgedOperation -> GetPayloadByID.
		//
		// CLIENT-INITIATED ops (negative IDs from QueueClientOperation: ability
		// activations, client-auth effects) carry payloads that ONLY the client
		// has cached. They must ship the FULL payload via OperationData so the
		// server can dispatch them -- a batch wrapper carrying only IDs would
		// silently drop them (server's IsValidClientOperation rejects the
		// wrapper, or ServerProcessAcknowledgedOperation can't find the payload).
		//
		// Conservative rule: batch only when EVERY queued ID is positive. A
		// single negative ID forces fallback to single-slot path, preserving
		// the legacy one-op-per-move semantics for client-initiated payloads.
		bool bAllServerBroadcast = true;
		for (const int OpID : ClientQueuedOperations)
		{
			if (OpID <= 0)
			{
				bAllServerBroadcast = false;
				break;
			}
		}

		if (!bAllServerBroadcast || ClientQueuedOperations.Num() == 1)
		{
			// Single-op fast path (pre-batch behaviour). Replicate the full
			// derived payload so ServerProcessOperation->IsValidClientOperation
			// passes on the receiving end -- sending only the base struct
			// causes the op to be silently dropped server-side.
			const int OperationIDToProcess = ClientQueuedOperations.Pop();
			OperationData = OperationPayloads.Contains(OperationIDToProcess)
				? OperationPayloads[OperationIDToProcess]
				: FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
			return;
		}

		// Multi-op server-broadcast-only path. Drains every queued OperationID
		// into a single FGMASBoundQueueV2BatchOperation wrapper. Sub-payloads
		// stay individually cached in OperationPayloads; ProcessOperation looks
		// them up by ID via the recursive batch dispatch. FIFO order preserves
		// application ordering visible to the user (LIFO Pop in the single-op
		// fast path is already an existing quirk, kept as-is for compat).
		FGMASBoundQueueV2BatchOperation Batch;
		Batch.SubOperationIDs.Reserve(ClientQueuedOperations.Num());
		for (const int OpID : ClientQueuedOperations)
		{
			if (OperationPayloads.Contains(OpID))
			{
				Batch.SubOperationIDs.Add(OpID);
			}
		}
		ClientQueuedOperations.Reset();

		if (Batch.SubOperationIDs.IsEmpty())
		{
			OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
			return;
		}

		OperationData = FInstancedStruct::Make<FGMASBoundQueueV2BatchOperation>(Batch);
	}
}

void FGMASBoundQueueV2::GenAncillaryTick(const float DeltaTime)
{
	CheckValidState();

	if (GMCMovementComponent->GetNetMode() >= NM_Client)
	{
		GMCMoveCounter++;
		ClearStaleOperationData();
	}
	
	// Tick all Server Queued Operations
	for (auto It = ServerQueuedBoundOperationsGracePeriods.CreateIterator(); It; ++It)
	{
		It.Value() -= DeltaTime;
		
		if (It.Value() <= 0) 
		{
			if (OperationPayloads.Contains(It.Key()))
			{
				OnServerOperationForced.Broadcast(OperationPayloads[It.Key()]);
				OperationPayloads.Remove(It.Key());
			}
			It.RemoveCurrent();
		}
	}
}

void FGMASBoundQueueV2::CacheOperationPayload(const int OperationID, const FInstancedStruct& Payload)
{
	OperationPayloads.Add(OperationID, Payload);
	OperationDataCacheExpiration.Add({OperationID, GMCMoveCounter});
}

void FGMASBoundQueueV2::QueueClientOperation(const int OperationID)
{
	ClientQueuedOperations.Add(OperationID);
}

void FGMASBoundQueueV2::QueueServerOperation(const int OperationID, const float Timeout)
{
	if (!OperationPayloads.Contains(OperationID))
	{
		UE_LOG(LogTemp, Error, TEXT("Tried to queue server operation, but server operation %d not found in payloads"), OperationID);
		return;
	}

	const FInstancedStruct QueuedOperation = OperationPayloads[OperationID];

	// Add to server timeout map
	ServerQueuedBoundOperationsGracePeriods.Add(OperationID, Timeout);

	// Notify
	OnServerOperationAdded.Broadcast(OperationID, QueuedOperation);
}

void FGMASBoundQueueV2::ServerAcknowledgeOperation(int ID)
{
	if (OperationPayloads.Contains(ID))
	{
		OperationPayloads.Remove(ID);
	}
	
	if (ServerQueuedBoundOperationsGracePeriods.Contains(ID))
	{
		ServerQueuedBoundOperationsGracePeriods.Remove(ID);
	}
}

void FGMASBoundQueueV2::CheckValidState() const
{
	// Server Logic
	if (GMCMovementComponent->GetNetMode() < NM_Client)
	{
		// Check Client Queued Operations is empty
		if (ClientQueuedOperations.Num() > 0)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("ClientQueuedOperations has %d pending operations on server"), ClientQueuedOperations.Num());
		}

		// Check OperationPayloads for invalid IDs (-1 is reserved for client-made operations)
		for (auto operation : OperationPayloads)
		{
			if (operation.Key < 0)
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("OperationPayloads has invalid operation ID %d on server"), operation.Key);
			}
		}
	}
	else
	{
		// Check Server Queued Operations is empty
		if (ServerQueuedBoundOperationsGracePeriods.Num() > 0)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("ServerQueuedBoundOperationsGracePeriods has %d pending operations on client"), ServerQueuedBoundOperationsGracePeriods.Num());;
		}
	}
}
