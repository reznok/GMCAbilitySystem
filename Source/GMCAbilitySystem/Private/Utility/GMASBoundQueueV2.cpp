#include "Utility/GMASBoundQueueV2.h"

#include <Utility/GMASBoundQueueV2.h>

#include "GMCAbilitySystem.h"
#include "GMCMovementUtilityComponent.h"


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

void FGMASBoundQueueV2::BindToGMC(UGMC_MovementUtilityCmp* MovementComponent)
{
	OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
	
	MovementComponent->BindInstancedStruct(
		OperationData,
		EGMC_PredictionMode::ClientAuth_InputOutput,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);
	
	GMCMovementComponent = MovementComponent;
}

void FGMASBoundQueueV2::GenPreLocalMoveExecution()
{
	// UE_LOG(LogTemp, Warning, TEXT("OperationDataType: %s"), *OperationData.GetScriptStruct()->GetName());
	// Client Logic
	if (GMCMovementComponent->GetNetMode() == NM_Client ||
		GMCMovementComponent->GetNetMode() == NM_Standalone ||
		GMCMovementComponent->IsLocallyControlledListenServerPawn())
	{
		// Get a pending operation
		if (ClientQueuedOperations.Num() > 0)
		{
			const int OperationIDToProcess = ClientQueuedOperations.Pop();
			if (OperationPayloads.Contains(OperationIDToProcess))
			{
				OperationData = OperationPayloads[OperationIDToProcess];
			}
		}
	}
}

void FGMASBoundQueueV2::GenPostLocalMoveExecution()
{
	OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>();
}

void FGMASBoundQueueV2::GenAncillaryTick(const float DeltaTime)
{

	///
	/// Server receiving the confirmation that a client processed a server-auth operation
	///
	if (GMCMovementComponent->GetNetMode() < NM_Client)
	{
		if (OperationData.IsValid())
		{
			// Ensure that the operationdata is a struct that inherits from basedata
			// Using pointer as this is untrusted client data
			const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();

			// Check if the operation is valid and it's an operation that originated from the server (ID != -1)
			if (BaseData && BaseData->OperationID != 0)
			{
				ServerAcknowledgeOperation(BaseData->OperationID);
			}
		}
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
