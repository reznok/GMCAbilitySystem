// Layer 2, two components: client and server ability instances converge through the ability-sync
// messages. The pair harness routes every message into an outbox the spec delivers, drops or reorders.

#include "Misc/AutomationTest.h"
#include "GMAS_AbilitySyncPairHarness.h"
#include "UGMAS_TestAbility.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FGMASAbilitySyncPairSpec,
	"GMAS.Unit.AbilitySync.Pair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FGMASAbilitySyncPairHarness Pair;

END_DEFINE_SPEC(FGMASAbilitySyncPairSpec)

void FGMASAbilitySyncPairSpec::Define()
{
	BeforeEach([this]()
	{
		Pair.Setup({ UGMAS_TestAbility::StaticClass() });
	});

	AfterEach([this]()
	{
		Pair.Teardown();
	});

	Describe("Harness", [this]()
	{
		It("activates one covered instance on both sides with the same derived ID", [this]()
		{
			constexpr int32 OpID = -3;
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);

			const int32 ExpectedID = UGMC_AbilitySystemComponent::DeriveAbilityIDFromOperationForTest(OpID, 0);
			UGMCAbility* ClientInstance = FGMASAbilitySyncPairHarness::FindByID(Pair.Client, ExpectedID);
			UGMCAbility* ServerInstance = FGMASAbilitySyncPairHarness::FindByID(Pair.Server, ExpectedID);
			if (!TestNotNull(TEXT("client instance"), ClientInstance) || !TestNotNull(TEXT("server instance"), ServerInstance))
			{
				return;
			}
			TestEqual(TEXT("client count"), Pair.Client->GetActiveAbilities().Num(), 1);
			TestEqual(TEXT("server count"), Pair.Server->GetActiveAbilities().Num(), 1);

			for (const UGMCAbility* Instance : { ClientInstance, ServerInstance })
			{
				TestEqual(TEXT("source operation"), Instance->GetSourceOperationID(), OpID);
				TestEqual(TEXT("candidate index"), Instance->GetSourceCandidateIndex(), 0);
				TestFalse(TEXT("not server-only"), Instance->IsServerOnly());
				TestFalse(TEXT("not client-authorized"), Instance->IsClientAuthorized());
				TestTrue(TEXT("covered"), Instance->IsCovered());
			}

			TestTrue(TEXT("client has a remote twin"), Pair.Client->HasRemoteAbilityTwinForTest());
			TestTrue(TEXT("server has a remote twin"), Pair.Server->HasRemoteAbilityTwinForTest());

			Pair.Tick();
			TestEqual(TEXT("no sync messages yet (plumbing only)"), Pair.Outbox.Num(), 0);
			TestEqual(TEXT("nothing held"), Pair.Client->GetHeldSyncMessagesForTest().Num() + Pair.Server->GetHeldSyncMessagesForTest().Num(), 0);
		});

		It("marks a direct authority activation server-only and not covered", [this]()
		{
			TestTrue(TEXT("activated"), Pair.Server->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
			const TMap<int, UGMCAbility*> Active = Pair.Server->GetActiveAbilities();
			for (const TPair<int, UGMCAbility*>& Entry : Active)
			{
				TestTrue(TEXT("server-only"), Entry.Value->IsServerOnly());
				TestFalse(TEXT("not covered"), Entry.Value->IsCovered());
				TestEqual(TEXT("no source operation"), Entry.Value->GetSourceOperationID(), 0);
			}
			TestEqual(TEXT("one instance"), Active.Num(), 1);
		});

		It("routes a sent message to the outbox and delivers it to the peer", [this]()
		{
			FGMASAbilitySyncMessage Message;
			Message.Type = EGMASAbilitySyncType::RequestDigest;
			Pair.Client->SendAbilitySyncForTest(Message, /*bToServer=*/true);
			TestEqual(TEXT("queued"), Pair.CountOutbox(EGMASAbilitySyncType::RequestDigest), 1);
			TestTrue(TEXT("to server"), Pair.Outbox.Num() == 1 && Pair.Outbox[0].Key);
			TestEqual(TEXT("delivered"), Pair.DeliverAll(), 1);
			TestEqual(TEXT("outbox drained"), Pair.Outbox.Num(), 0);
		});
	});
}

#endif
