#pragma once

#include "CoreMinimal.h"
#include "GMASAbilitySyncRules.generated.h"

/**
 * Ability convergence between the owning client and the server.
 *
 * An ability instance created from a bound-queue activation operation ("covered") exists on both
 * machines. These rules decide what each side does when an answer, an end or a digest arrives, so
 * the two instances always converge: disagreement ends the ability on both sides. World-free: the
 * component gathers the inputs, calls these functions and applies the actions.
 */

UENUM()
enum class EGMASAbilitySyncType : uint8
{
	Answer,
	Ended,
	Digest,
	RequestDigest
};

UENUM()
enum class EGMASAbilityAnswer : uint8
{
	Confirmed,
	Rejected
};

UENUM()
enum class EGMASAbilityEndKind : uint8
{
	Natural,
	Cancelled
};

/** One convergence message; Type selects which fields are meaningful. Carried by the two ability-sync RPCs. */
USTRUCT()
struct GMCABILITYSYSTEM_API FGMASAbilitySyncMessage
{
	GENERATED_BODY()

	UPROPERTY()
	EGMASAbilitySyncType Type = EGMASAbilitySyncType::Answer;

	// Answer
	UPROPERTY()
	int32 OperationID = 0;

	// Answer: index of the activated candidate; 0 when rejected
	UPROPERTY()
	int32 CandidateIndex = 0;

	// Answer (0 when rejected), Ended
	UPROPERTY()
	int32 AbilityID = 0;

	UPROPERTY()
	EGMASAbilityAnswer Answer = EGMASAbilityAnswer::Confirmed;

	UPROPERTY()
	EGMASAbilityEndKind EndKind = EGMASAbilityEndKind::Natural;

	// Digest
	UPROPERTY()
	TArray<int32> DigestIDs;
};

/** What one side knows about one of its covered ability instances. */
struct FGMASCoveredAbility
{
	int32 AbilityID = 0;
	int32 OperationID = 0;
	int32 CandidateIndex = 0;
	double StartConfirmClock = 0.0; // local confirm clock at creation
	bool bConfirmed = false;
	bool bClientAuthorized = false;
};

/** A message held because its instance does not exist yet. */
struct FGMASHeldSyncMessage
{
	FGMASAbilitySyncMessage Message;
	double ExpiresAt = 0.0; // local confirm clock
};

enum class EGMASSyncAction : uint8
{
	None,
	MarkConfirmed,
	EndNatural,
	EndCancelled,
	Hold
};

struct FGMASDigestActions
{
	TArray<int32> ReportServerOnly;   // client: ask the server to cancel these
	TArray<int32> EndClientOnly;      // client: cancel locally (and report)
};

namespace GMASAbilitySyncRules
{
	/** Client: an Answer arrived. Local = the client's live covered instance created from Answer.OperationID, or null. */
	GMCABILITYSYSTEM_API EGMASSyncAction OnAnswer(const FGMASCoveredAbility* Local, const FGMASAbilitySyncMessage& Answer);

	/** Either side: an Ended arrived for AbilityID. */
	GMCABILITYSYSTEM_API EGMASSyncAction OnEnded(bool bLocalLive, bool bRecentlyEnded, EGMASAbilityEndKind Kind);

	/** Client: a covered instance was just created; apply the held message for its operation or ID (if any). */
	GMCABILITYSYSTEM_API EGMASSyncAction OnCreatedWithHeld(const FGMASCoveredAbility& Created, const FGMASAbilitySyncMessage& Held);

	/** Client: a digest arrived. */
	GMCABILITYSYSTEM_API FGMASDigestActions OnDigest(TConstArrayView<FGMASCoveredAbility> Local, TConstArrayView<int32> DigestIDs,
		TConstArrayView<int32> RecentlyEnded, TConstArrayView<int32> HeldIDs, double Now, double MinAge, double Interval);

	/** Server: send a digest this interval? */
	GMCABILITYSYSTEM_API bool ShouldServerSendDigest(TConstArrayView<FGMASCoveredAbility> Server, double Now, double MinAge);

	/** Client: request a digest? */
	GMCABILITYSYSTEM_API bool ShouldClientRequestDigest(TConstArrayView<FGMASCoveredAbility> Local, double Now, double LastDigestAt,
		double LastRequestAt, double MinAge, double Interval);

	/** Client: confirm timeout for this instance? Client-authorized instances never time out. */
	GMCABILITYSYSTEM_API bool ShouldTimeOut(const FGMASCoveredAbility& Local, double Now, double Timeout);

	/** Drops held entries whose ExpiresAt <= Now; returns how many were dropped. */
	GMCABILITYSYSTEM_API int32 PruneHeld(TArray<FGMASHeldSyncMessage>& Held, double Now);
}
