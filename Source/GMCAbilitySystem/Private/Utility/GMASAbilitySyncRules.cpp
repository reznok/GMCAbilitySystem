#include "Utility/GMASAbilitySyncRules.h"

namespace GMASAbilitySyncRules
{
	EGMASSyncAction OnAnswer(const FGMASCoveredAbility* Local, const FGMASAbilitySyncMessage& Answer)
	{
		if (!Local)
		{
			return EGMASSyncAction::Hold;
		}
		if (Answer.Answer == EGMASAbilityAnswer::Rejected)
		{
			return EGMASSyncAction::EndCancelled;
		}
		// Confirmed: the same AbilityID means both sides activated the same candidate; another ID means the
		// server activated a different candidate for this operation, so the client's instance has no twin.
		return Local->AbilityID == Answer.AbilityID ? EGMASSyncAction::MarkConfirmed : EGMASSyncAction::EndCancelled;
	}

	EGMASSyncAction OnEnded(bool bLocalLive, bool bRecentlyEnded, EGMASAbilityEndKind Kind)
	{
		if (bLocalLive)
		{
			return Kind == EGMASAbilityEndKind::Natural ? EGMASSyncAction::EndNatural : EGMASSyncAction::EndCancelled;
		}
		// Ends are idempotent: an instance that already ended ignores the peer's end.
		return bRecentlyEnded ? EGMASSyncAction::None : EGMASSyncAction::Hold;
	}

	EGMASSyncAction OnCreatedWithHeld(const FGMASCoveredAbility& Created, const FGMASAbilitySyncMessage& Held)
	{
		switch (Held.Type)
		{
		case EGMASAbilitySyncType::Answer:
			return OnAnswer(&Created, Held);
		case EGMASAbilitySyncType::Ended:
			if (Held.AbilityID == Created.AbilityID)
			{
				return Held.EndKind == EGMASAbilityEndKind::Natural ? EGMASSyncAction::EndNatural : EGMASSyncAction::EndCancelled;
			}
			return EGMASSyncAction::None;
		default:
			return EGMASSyncAction::None;
		}
	}

	FGMASDigestActions OnDigest(TConstArrayView<FGMASCoveredAbility> Local, TConstArrayView<int32> DigestIDs,
		TConstArrayView<int32> RecentlyEnded, TConstArrayView<int32> HeldIDs, double Now, double MinAge, double Interval)
	{
		FGMASDigestActions Actions;

		for (const int32 ID : DigestIDs)
		{
			const bool bLive = Local.ContainsByPredicate([ID](const FGMASCoveredAbility& Covered) { return Covered.AbilityID == ID; });
			if (!bLive && !RecentlyEnded.Contains(ID) && !HeldIDs.Contains(ID))
			{
				Actions.ReportServerOnly.AddUnique(ID);
			}
		}

		// The extra interval is a margin for a digest that was in flight while the instance was created.
		const double ClientOnlyAge = MinAge + Interval;
		for (const FGMASCoveredAbility& Covered : Local)
		{
			if (Now - Covered.StartConfirmClock >= ClientOnlyAge && !DigestIDs.Contains(Covered.AbilityID))
			{
				Actions.EndClientOnly.AddUnique(Covered.AbilityID);
			}
		}

		return Actions;
	}

	bool ShouldServerSendDigest(TConstArrayView<FGMASCoveredAbility> Server, double Now, double MinAge)
	{
		return Server.ContainsByPredicate([Now, MinAge](const FGMASCoveredAbility& Covered)
		{
			return Now - Covered.StartConfirmClock >= MinAge;
		});
	}

	bool ShouldClientRequestDigest(TConstArrayView<FGMASCoveredAbility> Local, double Now, double LastDigestAt,
		double LastRequestAt, double MinAge, double Interval)
	{
		const bool bAnyOld = Local.ContainsByPredicate([Now, MinAge](const FGMASCoveredAbility& Covered)
		{
			return Now - Covered.StartConfirmClock >= MinAge;
		});
		return bAnyOld && Now - LastDigestAt >= 2.0 * Interval && Now - LastRequestAt >= Interval;
	}

	bool ShouldTimeOut(const FGMASCoveredAbility& Local, double Now, double Timeout)
	{
		return !Local.bConfirmed && !Local.bClientAuthorized && Now - Local.StartConfirmClock > Timeout;
	}

	int32 PruneHeld(TArray<FGMASHeldSyncMessage>& Held, double Now)
	{
		return Held.RemoveAll([Now](const FGMASHeldSyncMessage& Entry) { return Entry.ExpiresAt <= Now; });
	}
}
