// Layer 1: the world-free ability-sync rules. Every branch of GMASAbilitySyncRules, without a component.

#include "Misc/AutomationTest.h"
#include "Utility/GMASAbilitySyncRules.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace GMASAbilitySyncRulesSpecLocal
{
	FGMASCoveredAbility MakeCovered(int32 AbilityID, int32 OperationID, double StartConfirmClock,
		bool bConfirmed = false, bool bClientAuthorized = false)
	{
		FGMASCoveredAbility Covered;
		Covered.AbilityID = AbilityID;
		Covered.OperationID = OperationID;
		Covered.CandidateIndex = 0;
		Covered.StartConfirmClock = StartConfirmClock;
		Covered.bConfirmed = bConfirmed;
		Covered.bClientAuthorized = bClientAuthorized;
		return Covered;
	}

	FGMASAbilitySyncMessage MakeAnswer(int32 OperationID, int32 AbilityID, EGMASAbilityAnswer Answer)
	{
		FGMASAbilitySyncMessage Message;
		Message.Type = EGMASAbilitySyncType::Answer;
		Message.OperationID = OperationID;
		Message.AbilityID = Answer == EGMASAbilityAnswer::Confirmed ? AbilityID : 0;
		Message.Answer = Answer;
		return Message;
	}

	FGMASAbilitySyncMessage MakeEnded(int32 AbilityID, EGMASAbilityEndKind Kind)
	{
		FGMASAbilitySyncMessage Message;
		Message.Type = EGMASAbilitySyncType::Ended;
		Message.AbilityID = AbilityID;
		Message.EndKind = Kind;
		return Message;
	}
}

BEGIN_DEFINE_SPEC(FGMASAbilitySyncRulesSpec,
	"GMAS.Unit.AbilitySync.Rules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FGMASAbilitySyncRulesSpec)

void FGMASAbilitySyncRulesSpec::Define()
{
	using namespace GMASAbilitySyncRulesSpecLocal;

	Describe("OnAnswer", [this]()
	{
		It("holds an answer for an operation with no local instance", [this]()
		{
			const FGMASAbilitySyncMessage Answer = MakeAnswer(-5, 42, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnAnswer(nullptr, Answer), EGMASSyncAction::Hold);
		});

		It("cancels the local instance on Rejected", [this]()
		{
			const FGMASCoveredAbility Local = MakeCovered(42, -5, 0.0);
			const FGMASAbilitySyncMessage Answer = MakeAnswer(-5, 0, EGMASAbilityAnswer::Rejected);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnAnswer(&Local, Answer), EGMASSyncAction::EndCancelled);
		});

		It("marks confirmed on Confirmed with the same AbilityID", [this]()
		{
			const FGMASCoveredAbility Local = MakeCovered(42, -5, 0.0);
			const FGMASAbilitySyncMessage Answer = MakeAnswer(-5, 42, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnAnswer(&Local, Answer), EGMASSyncAction::MarkConfirmed);
		});

		It("cancels the local instance on Confirmed with another AbilityID (candidate mismatch)", [this]()
		{
			const FGMASCoveredAbility Local = MakeCovered(42, -5, 0.0);
			const FGMASAbilitySyncMessage Answer = MakeAnswer(-5, 43, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnAnswer(&Local, Answer), EGMASSyncAction::EndCancelled);
		});
	});

	Describe("OnEnded", [this]()
	{
		It("ends a live instance naturally for a Natural end", [this]()
		{
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnEnded(true, false, EGMASAbilityEndKind::Natural), EGMASSyncAction::EndNatural);
		});

		It("cancels a live instance for a Cancelled end", [this]()
		{
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnEnded(true, false, EGMASAbilityEndKind::Cancelled), EGMASSyncAction::EndCancelled);
		});

		It("does nothing for an instance that already ended (idempotent)", [this]()
		{
			TestEqual(TEXT("natural"), GMASAbilitySyncRules::OnEnded(false, true, EGMASAbilityEndKind::Natural), EGMASSyncAction::None);
			TestEqual(TEXT("cancelled"), GMASAbilitySyncRules::OnEnded(false, true, EGMASAbilityEndKind::Cancelled), EGMASSyncAction::None);
		});

		It("holds an end for an instance that does not exist yet", [this]()
		{
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnEnded(false, false, EGMASAbilityEndKind::Natural), EGMASSyncAction::Hold);
		});
	});

	Describe("OnCreatedWithHeld", [this]()
	{
		It("applies a held Confirmed answer as a confirmation", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeAnswer(-5, 42, EGMASAbilityAnswer::Confirmed)), EGMASSyncAction::MarkConfirmed);
		});

		It("applies a held Rejected answer as an immediate cancel", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeAnswer(-5, 0, EGMASAbilityAnswer::Rejected)), EGMASSyncAction::EndCancelled);
		});

		It("applies a held mismatched Confirmed answer as a cancel", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeAnswer(-5, 43, EGMASAbilityAnswer::Confirmed)), EGMASSyncAction::EndCancelled);
		});

		It("applies a held Natural end for its AbilityID as a natural end", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeEnded(42, EGMASAbilityEndKind::Natural)), EGMASSyncAction::EndNatural);
		});

		It("applies a held Cancelled end for its AbilityID as a cancel", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeEnded(42, EGMASAbilityEndKind::Cancelled)), EGMASSyncAction::EndCancelled);
		});

		It("ignores a held end for another AbilityID", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			TestEqual(TEXT("action"), GMASAbilitySyncRules::OnCreatedWithHeld(Created,
				MakeEnded(43, EGMASAbilityEndKind::Natural)), EGMASSyncAction::None);
		});

		It("ignores a held digest or digest request", [this]()
		{
			const FGMASCoveredAbility Created = MakeCovered(42, -5, 0.0);
			FGMASAbilitySyncMessage Digest;
			Digest.Type = EGMASAbilitySyncType::Digest;
			Digest.DigestIDs = { 42 };
			TestEqual(TEXT("digest"), GMASAbilitySyncRules::OnCreatedWithHeld(Created, Digest), EGMASSyncAction::None);
			FGMASAbilitySyncMessage Request;
			Request.Type = EGMASAbilitySyncType::RequestDigest;
			TestEqual(TEXT("request"), GMASAbilitySyncRules::OnCreatedWithHeld(Created, Request), EGMASSyncAction::None);
		});
	});

	Describe("OnDigest", [this]()
	{
		const double MinAge = 1.0;
		const double Interval = 1.0;

		It("reports a digest ID that is not live, not held and not recently ended as server-only", [this, MinAge, Interval]()
		{
			const TArray<FGMASCoveredAbility> Local = { MakeCovered(1, -1, 9.5) };
			const TArray<int32> DigestIDs = { 1, 2 };
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest(Local, DigestIDs, {}, {}, 10.0, MinAge, Interval);
			TestEqual(TEXT("server-only count"), Actions.ReportServerOnly.Num(), 1);
			TestTrue(TEXT("reports 2"), Actions.ReportServerOnly.Contains(2));
			TestEqual(TEXT("client-only count"), Actions.EndClientOnly.Num(), 0);
		});

		It("does not report a digest ID that recently ended locally", [this, MinAge, Interval]()
		{
			const TArray<int32> DigestIDs = { 2 };
			const TArray<int32> RecentlyEnded = { 2 };
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest({}, DigestIDs, RecentlyEnded, {}, 10.0, MinAge, Interval);
			TestEqual(TEXT("server-only count"), Actions.ReportServerOnly.Num(), 0);
		});

		It("does not report a digest ID with a held message", [this, MinAge, Interval]()
		{
			const TArray<int32> DigestIDs = { 2 };
			const TArray<int32> HeldIDs = { 2 };
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest({}, DigestIDs, {}, HeldIDs, 10.0, MinAge, Interval);
			TestEqual(TEXT("server-only count"), Actions.ReportServerOnly.Num(), 0);
		});

		It("ends a local instance missing from the digest once it is MinAge + Interval old", [this, MinAge, Interval]()
		{
			const TArray<FGMASCoveredAbility> Local = { MakeCovered(7, -3, 8.0) };   // age 2.0
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest(Local, {}, {}, {}, 10.0, MinAge, Interval);
			TestEqual(TEXT("client-only count"), Actions.EndClientOnly.Num(), 1);
			TestTrue(TEXT("ends 7"), Actions.EndClientOnly.Contains(7));
		});

		It("keeps a local instance missing from the digest just inside the age margin", [this, MinAge, Interval]()
		{
			const TArray<FGMASCoveredAbility> Local = { MakeCovered(7, -3, 10.0 - (MinAge + Interval - 0.01)) };
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest(Local, {}, {}, {}, 10.0, MinAge, Interval);
			TestEqual(TEXT("client-only count"), Actions.EndClientOnly.Num(), 0);
		});

		It("keeps an old local instance present in the digest", [this, MinAge, Interval]()
		{
			const TArray<FGMASCoveredAbility> Local = { MakeCovered(7, -3, 0.0) };
			const TArray<int32> DigestIDs = { 7 };
			const FGMASDigestActions Actions = GMASAbilitySyncRules::OnDigest(Local, DigestIDs, {}, {}, 10.0, MinAge, Interval);
			TestEqual(TEXT("client-only count"), Actions.EndClientOnly.Num(), 0);
			TestEqual(TEXT("server-only count"), Actions.ReportServerOnly.Num(), 0);
		});
	});

	Describe("ShouldServerSendDigest", [this]()
	{
		It("sends when any covered instance is at least MinAge old", [this]()
		{
			const TArray<FGMASCoveredAbility> Server = { MakeCovered(1, -1, 9.8), MakeCovered(2, -2, 9.0) };
			TestTrue(TEXT("send"), GMASAbilitySyncRules::ShouldServerSendDigest(Server, 10.0, 1.0));
		});

		It("does not send when every covered instance is younger than MinAge", [this]()
		{
			const TArray<FGMASCoveredAbility> Server = { MakeCovered(1, -1, 9.8) };
			TestFalse(TEXT("young"), GMASAbilitySyncRules::ShouldServerSendDigest(Server, 10.0, 1.0));
			TestFalse(TEXT("empty"), GMASAbilitySyncRules::ShouldServerSendDigest({}, 10.0, 1.0));
		});
	});

	Describe("ShouldClientRequestDigest", [this]()
	{
		const TArray<FGMASCoveredAbility> OldLocal = { MakeCovered(1, -1, 5.0) };

		It("requests when an old instance exists, no digest for 2 intervals and no request for 1 interval", [this, OldLocal]()
		{
			TestTrue(TEXT("request"), GMASAbilitySyncRules::ShouldClientRequestDigest(OldLocal, 10.0, 8.0, 9.0, 1.0, 1.0));
		});

		It("does not request without an instance at least MinAge old", [this]()
		{
			const TArray<FGMASCoveredAbility> Young = { MakeCovered(1, -1, 9.5) };
			TestFalse(TEXT("young"), GMASAbilitySyncRules::ShouldClientRequestDigest(Young, 10.0, 0.0, 0.0, 1.0, 1.0));
		});

		It("does not request while a digest arrived within 2 intervals", [this, OldLocal]()
		{
			TestFalse(TEXT("recent digest"), GMASAbilitySyncRules::ShouldClientRequestDigest(OldLocal, 10.0, 8.5, 0.0, 1.0, 1.0));
		});

		It("does not request again within one interval of the last request", [this, OldLocal]()
		{
			TestFalse(TEXT("rate limited"), GMASAbilitySyncRules::ShouldClientRequestDigest(OldLocal, 10.0, 0.0, 9.5, 1.0, 1.0));
		});
	});

	Describe("ShouldTimeOut", [this]()
	{
		It("times out an unconfirmed instance past the timeout", [this]()
		{
			TestTrue(TEXT("timeout"), GMASAbilitySyncRules::ShouldTimeOut(MakeCovered(1, -1, 0.0), 2.5, 2.0));
		});

		It("does not time out at or before the timeout", [this]()
		{
			TestFalse(TEXT("at"), GMASAbilitySyncRules::ShouldTimeOut(MakeCovered(1, -1, 0.0), 2.0, 2.0));
			TestFalse(TEXT("before"), GMASAbilitySyncRules::ShouldTimeOut(MakeCovered(1, -1, 0.0), 1.0, 2.0));
		});

		It("never times out a confirmed instance", [this]()
		{
			TestFalse(TEXT("confirmed"), GMASAbilitySyncRules::ShouldTimeOut(MakeCovered(1, -1, 0.0, true), 100.0, 2.0));
		});

		It("never times out a client-authorized instance", [this]()
		{
			TestFalse(TEXT("client-auth"), GMASAbilitySyncRules::ShouldTimeOut(MakeCovered(1, -1, 0.0, false, true), 100.0, 2.0));
		});
	});

	Describe("PruneHeld", [this]()
	{
		It("drops expired entries and keeps unexpired ones", [this]()
		{
			TArray<FGMASHeldSyncMessage> Held;
			Held.Add({ MakeEnded(1, EGMASAbilityEndKind::Natural), 4.0 });
			Held.Add({ MakeEnded(2, EGMASAbilityEndKind::Natural), 5.0 });
			Held.Add({ MakeEnded(3, EGMASAbilityEndKind::Natural), 6.0 });
			const int32 Dropped = GMASAbilitySyncRules::PruneHeld(Held, 5.0);
			TestEqual(TEXT("dropped"), Dropped, 2);
			TestEqual(TEXT("kept"), Held.Num(), 1);
			if (Held.Num() == 1)
			{
				TestEqual(TEXT("kept id"), Held[0].Message.AbilityID, 3);
			}
		});

		It("keeps everything when nothing expired", [this]()
		{
			TArray<FGMASHeldSyncMessage> Held;
			Held.Add({ MakeEnded(1, EGMASAbilityEndKind::Natural), 6.0 });
			TestEqual(TEXT("dropped"), GMASAbilitySyncRules::PruneHeld(Held, 5.0), 0);
			TestEqual(TEXT("kept"), Held.Num(), 1);
		});
	});
}

#endif
