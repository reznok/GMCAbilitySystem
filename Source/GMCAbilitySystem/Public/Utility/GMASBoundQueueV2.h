#pragma once

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5
#include "StructUtils/InstancedStruct.h"  // UE 5.5+
#else
#include "InstancedStruct.h"              // UE 5.4 and earlier
#endif
#include "GMASBoundQueueV2_Operations.h"
#include "GMCMovementUtilityComponent.h"
#include "GMASBoundQueueV2.generated.h"

class UGMC_MovementUtilityCmp;
class UGMCMovementComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnServerOperationAdded, int, OperationID, FInstancedStruct, OperationData);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnServerOperationForced, FInstancedStruct, OperationData);


USTRUCT()
struct FOperationDataCacheExpiration
{
	GENERATED_BODY()
	
	// Operation ID
	int OperationID = -1;

	// The GMC move # (GMCMoveCounter) when this operation was added
	int64 ModeAddedAt = -1;
};

USTRUCT()
struct GMCABILITYSYSTEM_API FGMASBoundQueueV2
{
	GENERATED_BODY()
	// Events
	FOnServerOperationAdded OnServerOperationAdded;
	FOnServerOperationForced OnServerOperationForced;

	UPROPERTY()
	UGMC_MovementUtilityCmp* GMCMovementComponent = nullptr;

	// Every time a GMC move is processed, this counter is incremented
	// Used to expire stale operation data
	int64 GMCMoveCounter = 0;
	TArray<FOperationDataCacheExpiration> OperationDataCacheExpiration;

	void ClearStaleOperationData();
	
	int NextOperationID = 0;
	
	// Get the next operation ID
	// Any positive ID is a server generated operation
	// Any negative ID is a client generated operation
	int GetNextOperationID()
	{
		// Reachable through MakeOperationData before BindToGMC: without a movement component this
		// side cannot be a client, so it takes a positive (server) id.
		if (!GMCMovementComponent || GMCMovementComponent->GetNetMode() != NM_Client)
		{
			return ++NextOperationID;
		}
		return --NextOperationID;
	}

	// OperationID, OperationPayload
protected:
	TMap<int, FInstancedStruct> OperationPayloads;

public:
	// Accessors for OperationPayloads
	
	void RemovePayloadByID(const int OperationID)
	{
		if (OperationPayloads.Contains(OperationID))
		{
			OperationPayloads.Remove(OperationID);
		}
		ReportedInvalidPayloadIDs.Remove(OperationID);
	}

	// Make a GetOperationByID
	FInstancedStruct GetPayloadByID(const int OperationID)
	{
		if (OperationPayloads.Contains(OperationID))
		{
			return OperationPayloads[OperationID];
		}
		return FInstancedStruct();
	}

	bool HasPayloadByID(const int OperationID) const
	{
		return OperationPayloads.Contains(OperationID);
	}

	int GetPayloadCount() const
	{
		return OperationPayloads.Num();
	}

	
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
	int BI_OperationData;
	FInstancedStruct OperationData;
	//// End GMC Bound

	void CacheOperationPayload(const int OperationID, const FInstancedStruct& Payload);
	
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

		// Add to payload cache to reference it later
		CacheOperationPayload(BuiltData.OperationID, OutStruct);
		
		return BuiltData.OperationID;
	}

	// Operation types that the client can supply for the server to run
	// Treat these as unsafe client-supplied data
	TArray<UScriptStruct*> ValidClientInputOperationTypes = {
		FGMASBoundQueueV2AbilityActivationOperation::StaticStruct(),
		FGMASBoundQueueV2AcknowledgeOperation::StaticStruct(),
		FGMASBoundQueueV2BatchAcknowledgeOperation::StaticStruct(),
		FGMASBoundQueueV2ClientAuthAbilityActivationOperation::StaticStruct(),
		FGMASBoundQueueV2ClientAuthEffectOperation::StaticStruct(),
		FGMASBoundQueueV2ClientAuthRemoveEffectOperation::StaticStruct()
	};

	bool IsValidGMASOperation(const FInstancedStruct& Data) const;
	
	bool IsValidClientOperation(const FInstancedStruct& Data) const;

	// Only negative ids are client-made. Positive ids are server-made (confirmed to the client through
	// the acknowledgement path) and 0 is an empty slot or a batch wrapper.
	static bool IsClientMadeOperationID(int OperationID) { return OperationID < 0; }

	// Queue a Client operation
	void QueueClientOperation(const int OperationID);

	// Queue a server operation; Timeout is the ack grace in seconds (UGMASNetworkTimingSettings::ServerOperationGraceSeconds for pawns with a client, 0 otherwise).
	void QueueServerOperation(const int OperationID, const float Timeout);
	
	bool CurrentOperationIsOfType(const UScriptStruct* T) const
	{
		return OperationData.GetScriptStruct() == T;
	}

	// Process a server operation that the client has sent an ack for
	void ServerAcknowledgeOperation(int ID);

	// --- Benign-reprocess dedup -------------------------------------------------
	// The bound OperationData slot replicates client->server and is re-read by the
	// authority on BOTH the prediction tick and the ancillary tick (and on every
	// server tick until a fresh client move overwrites it). The first pass over a
	// FGMASBoundQueueV2BatchOperation applies each sub-op and drains its cached
	// payload (RemovePayloadByID on authority). Every later pass therefore finds
	// those payloads gone -- NOT because the op never arrived, but because we
	// already applied it. ProcessOperation's batch path consults this ring to tell
	// that benign reprocess (silent skip) apart from a genuine first-time missing
	// payload (true state divergence -> loud error preserved).
	//
	// Bounded ring of the most-recently dispatched sub-op IDs. A batch is only ever
	// reprocessed within its own short lifetime (one move's prediction+ancillary
	// tick pair, a handful of server ticks), so a small ring is always large enough
	// to recognise the reprocess; older entries are evicted FIFO. IDs are monotonic for
	// one connection (GetNextOperationID does not reuse a value), so an ID found here was
	// genuinely processed -- there are no false benign-skips. A reconnecting client restarts
	// its ids, so the server empties the ring then (ResetForNewConnection). Netmode-independent
	// (does not depend on GMCMoveCounter, which never advances on a listen server).
	static constexpr int32 MaxRecentlyProcessedOperations = 256;
	TArray<int32> RecentlyProcessedOperationIDs;

	// Record a sub-op as successfully dispatched on this side.
	void MarkOperationProcessed(int32 OperationID);

	// A new owning connection took over (kept-pawn reconnect): its client restarts its operation
	// ids at -1, so forget the processed ring and every cached client-made (negative id) payload.
	void ResetForNewConnection();

	// True if this sub-op was dispatched recently (i.e. a missing payload is a
	// benign reprocess, not a genuine divergence).
	bool WasOperationRecentlyProcessed(int32 OperationID) const;

	// Operations (referenced by ID to OperationPayloads) that the Client has queued
	// Key: Operation Id
	TArray<int> ClientQueuedOperations;

	// Operations that the server has sent to the client but haven't been acknowledged yet
	// If the client doesn't acknowledge the operation in time, the server will force it
	// Map: OperationId -> GracePeriod
	TMap<int, float> ServerQueuedBoundOperationsGracePeriods;

	// Transient flag set during batch dispatch in ProcessOperation. Suppresses
	// per-sub-op AckOp writes to OperationData (which is a single bound slot
	// that would race for the slot if N sub-ops each wrote their own ack).
	// The final FGMASBoundQueueV2BatchAcknowledgeOperation is written once at
	// the end of the batch, carrying every successfully-processed sub-op ID.
	bool bInBatchDispatch = false;

	// Runs checks on the current state of the queue and logs any issues found. Each issue is
	// reported once (per id, or until the offending queue clears) instead of every tick.
	void CheckValidState() const;
	mutable TSet<int> ReportedInvalidPayloadIDs;
	mutable bool bReportedClientQueuedOnServer = false;
	mutable bool bReportedServerQueuedOnClient = false;
};

// Operations
#pragma region Operations

#pragma endregion Operations
