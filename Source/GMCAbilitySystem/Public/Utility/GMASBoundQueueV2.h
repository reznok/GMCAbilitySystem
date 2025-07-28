#pragma once

#include "StructUtils/InstancedStruct.h"
#include "GMASBoundQueueV2_Operations.h"
#include "GMASBoundQueueV2.generated.h"

class UGMC_MovementUtilityCmp;
class UGMCMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerOperationAdded, int, OperationID, FInstancedStruct, OperationData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerOperationForced, FInstancedStruct, OperationData);

USTRUCT()
struct FGMASBoundQueueV2
{
	GENERATED_BODY()
	// Events
	FOnServerOperationAdded OnServerOperationAdded;
	FOnServerOperationForced OnServerOperationForced;

	bool bIsServer{false};

	int NextOperationID = 0;
	
	// Get the next operation ID
	// Any positive ID is a server operation
	// Any negative ID is a client operation
	int GetNextOperationID()
	{
		if (bIsServer)
		{
			return ++NextOperationID;
		}
		return --NextOperationID;
	}

	// OperationID, OperationPayload
	TMap<int, FInstancedStruct> OperationPayloads;
	TArray<int> OperationQueue;
	
	// GMC
	void BindToGMC(UGMC_MovementUtilityCmp* MovementComponent);
	void GenPreLocalMoveExecution();
	void GenAncillaryTick(float DeltaTime);
	
	//// GMC Bound
	//
	// Operation Data used to actually process the operation
	// These are only ever sent via RPC from the server to the client then cached
	// GMC moves will access the caches instead of storing it all in moves
	FInstancedStruct OperationData;
	//// End GMC Bound


	
	// Wrappers for building instanced structs for each data type
	// Adds the Operation ID to the data and stores the payload in OperationPayloads
	template <typename T>
	int MakeOperationData(const T& Data)
	{
		static_assert(TIsDerivedFrom<T, FGMASBoundQueueV2OperationBaseData>::IsDerived, "T must be derived from FGMASBoundQueueV2BaseData");

		T BuiltData = Data;
		BuiltData.OperationID = GetNextOperationID();
		
		FInstancedStruct OutStruct;
		OutStruct.InitializeAs<T>(BuiltData);

		// Add to payload Map to reference it later
		OperationPayloads.Add(BuiltData.OperationID, OutStruct);
		
		return BuiltData.OperationID;
	}

	// Operation types that the client can supply for the server to run
	// Treat these as unsafe client-supplied data
	TArray<UScriptStruct*> ValidClientInputOperationTypes = {
		FGMASBoundQueueV2AbilityActivationOperation::StaticStruct(),
	};

	bool IsValidClientOperation(const FInstancedStruct& Data) const;

	// Queue a Client operation
	void QueueClientOperation(const int OperationID);

	// Queue a ServerAuth operation
	void QueueServerOperation(const int OperationID, const float Timeout = 1.0f);
	
	bool CurrentOperationIsOfType(const UScriptStruct* T) const
	{
		return OperationData.GetScriptStruct() == T;
	}

	void ServerAcknowledgeOperation(int ID);
	
	// Operations (referenced by ID to OperationPayloads) that the Client has queued
	// Key: Operation Id, Bool: bSendOperationDataToServer
	TArray<int> ClientQueuedOperations;

	// Operations that the server has sent to the client but haven't been acknowledged yet
	// If the client doesn't acknowledge the operation in time, the server will force it
	// Map: OperationId -> GracePeriod
	TMap<int, float> ServerQueuedBoundOperationsGracePeriods;
};

// Operations
#pragma region Operations

#pragma endregion Operations
