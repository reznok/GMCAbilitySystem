// Layer 2, two components: client and server ability instances converge through the ability-sync
// messages. The pair harness routes every message into an outbox the spec delivers, drops or reorders.

#include "Misc/AutomationTest.h"
#include "GMAS_AbilitySyncPairHarness.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestAbilityB.h"
#include "UGMAS_TestEventRecorder.h"
#include "HAL/IConsoleManager.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FGMASAbilitySyncPairSpec,
	"GMAS.Unit.AbilitySync.Pair",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FGMASAbilitySyncPairHarness Pair;
	UGMAS_TestEventRecorder* ClientRecorder = nullptr;

	static constexpr int32 OpID = -3;

	// Ability and gate tags. Candidate 0 (UGMAS_TestAbility) is refused while BlockA is active,
	// candidate 1 (UGMAS_TestAbilityB) while BlockB is: giving one side a block tag the other lacks
	// makes the two sides pick different candidates, or refuse, for the same operation.
	static FGameplayTag AbilityTagA()
	{
		static FNativeGameplayTag STag(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
			TEXT("GMAS.Test.AbilitySync.AbilityA"), TEXT("Ability tag for the ability-sync pair specs"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		return STag.GetTag();
	}
	static FGameplayTag AbilityTagB()
	{
		static FNativeGameplayTag STag(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
			TEXT("GMAS.Test.AbilitySync.AbilityB"), TEXT("Ability tag for the ability-sync pair specs"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		return STag.GetTag();
	}
	static FGameplayTag BlockA()
	{
		static FNativeGameplayTag STag(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
			TEXT("GMAS.Test.AbilitySync.BlockA"), TEXT("Blocks candidate 0 in the ability-sync pair specs"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		return STag.GetTag();
	}
	static FGameplayTag BlockB()
	{
		static FNativeGameplayTag STag(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
			TEXT("GMAS.Test.AbilitySync.BlockB"), TEXT("Blocks candidate 1 in the ability-sync pair specs"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		return STag.GetTag();
	}

	static int32 IDFor(int32 CandidateIndex)
	{
		return UGMC_AbilitySystemComponent::DeriveAbilityIDFromOperationForTest(OpID, CandidateIndex);
	}

	UGMAS_TestAbility* ClientA() const
	{
		return Cast<UGMAS_TestAbility>(FGMASAbilitySyncPairHarness::FindByID(Pair.Client, IDFor(0)));
	}

	UGMAS_TestAbility* ServerA() const
	{
		return Cast<UGMAS_TestAbility>(FGMASAbilitySyncPairHarness::FindByID(Pair.Server, IDFor(0)));
	}

	// The single queued message of Type in the given direction, or null when there is none or more than one.
	const TPair<bool, FGMASAbilitySyncMessage>* OnlyOutbox(EGMASAbilitySyncType Type, bool bToServer = false) const
	{
		const TPair<bool, FGMASAbilitySyncMessage>* Found = nullptr;
		for (const TPair<bool, FGMASAbilitySyncMessage>& Entry : Pair.Outbox)
		{
			if (Entry.Value.Type != Type || Entry.Key != bToServer) { continue; }
			if (Found) { return nullptr; }
			Found = &Entry;
		}
		return Found;
	}

	int32 CountOutbox(EGMASAbilitySyncType Type, bool bToServer) const
	{
		int32 Count = 0;
		for (const TPair<bool, FGMASAbilitySyncMessage>& Entry : Pair.Outbox)
		{
			Count += (Entry.Value.Type == Type && Entry.Key == bToServer) ? 1 : 0;
		}
		return Count;
	}

	// Sets one of the fault-injection console variables; false when it does not exist (shipping, or missing).
	static bool SetDebugCVar(const TCHAR* Name, int32 Value)
	{
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!CVar) { return false; }
		CVar->Set(Value, ECVF_SetByCode);
		return true;
	}

END_DEFINE_SPEC(FGMASAbilitySyncPairSpec)

void FGMASAbilitySyncPairSpec::Define()
{
	BeforeEach([this]()
	{
		UGMAS_TestAbility* CDOA = GetMutableDefault<UGMAS_TestAbility>();
		CDOA->bEndOnBegin = false;
		CDOA->AbilityTag = AbilityTagA();
		CDOA->ActivationBlockedTags = FGameplayTagContainer(BlockA());
		CDOA->CooldownTime = 0.f;
		UGMAS_TestAbilityB* CDOB = GetMutableDefault<UGMAS_TestAbilityB>();
		CDOB->AbilityTag = AbilityTagB();
		CDOB->ActivationBlockedTags = FGameplayTagContainer(BlockB());

		Pair.Setup({ UGMAS_TestAbility::StaticClass(), UGMAS_TestAbilityB::StaticClass() });
		ClientRecorder = NewObject<UGMAS_TestEventRecorder>(GetTransientPackage());
		ClientRecorder->AddToRoot();
		ClientRecorder->Bind(Pair.Client);
	});

	AfterEach([this]()
	{
		Pair.Teardown();
		ClientRecorder->RemoveFromRoot();
		ClientRecorder = nullptr;
		for (UGMCAbility* CDO : { static_cast<UGMCAbility*>(GetMutableDefault<UGMAS_TestAbility>()),
			static_cast<UGMCAbility*>(GetMutableDefault<UGMAS_TestAbilityB>()) })
		{
			CDO->AbilityTag = FGameplayTag();
			CDO->ActivationBlockedTags = FGameplayTagContainer();
			CDO->CooldownTime = 0.f;
		}
		GetMutableDefault<UGMAS_TestAbility>()->bEndOnBegin = false;
		GetMutableDefault<UGMAS_TestAbility>()->bActivateOnMovementTick = true;
		SetDebugCVar(TEXT("GMAS.Debug.DropAbilityAnswers"), 0);
		SetDebugCVar(TEXT("GMAS.Debug.DropAbilityEnds"), 0);
		SetDebugCVar(TEXT("GMAS.Debug.DelayAbilityMessagesMs"), 0);
	});

	Describe("Harness", [this]()
	{
		It("activates one covered instance on both sides with the same derived ID", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);

			const int32 ExpectedID = IDFor(0);
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
			TestEqual(TEXT("delivered: the request, then the server's digest reply"), Pair.DeliverAll(), 2);
			TestEqual(TEXT("outbox drained"), Pair.Outbox.Num(), 0);
		});
	});

	Describe("Answer", [this]()
	{
		It("confirms the client instance; it does not time out after 3 s", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			const TPair<bool, FGMASAbilitySyncMessage>* Answer = OnlyOutbox(EGMASAbilitySyncType::Answer);
			if (!TestNotNull(TEXT("exactly one answer, server -> client"), Answer)) { return; }
			TestEqual(TEXT("confirmed"), Answer->Value.Answer, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("operation"), Answer->Value.OperationID, OpID);
			TestEqual(TEXT("candidate"), Answer->Value.CandidateIndex, 0);
			TestEqual(TEXT("ability id"), Answer->Value.AbilityID, IDFor(0));
			TestEqual(TEXT("only the answer"), Pair.Outbox.Num(), 1);

			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			TestTrue(TEXT("confirmed"), Client->IsServerConfirmed());

			Pair.AdvanceClocks(3.0);
			Pair.Tick();
			TestEqual(TEXT("still running"), Client->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("no cancel"), Client->CancelAbilityEventCount, 0);
			TestEqual(TEXT("nothing else sent"), Pair.Outbox.Num(), 0);
		});

		It("answers Rejected when every candidate fails its tag gate; the client cancels within one delivery", [this]()
		{
			Pair.Server->AddActiveTag(BlockA());
			Pair.Server->AddActiveTag(BlockB());
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestEqual(TEXT("no server instance"), Pair.Server->GetActiveAbilities().Num(), 0);

			const TPair<bool, FGMASAbilitySyncMessage>* Answer = OnlyOutbox(EGMASAbilitySyncType::Answer);
			if (!TestNotNull(TEXT("exactly one answer"), Answer)) { return; }
			TestEqual(TEXT("rejected"), Answer->Value.Answer, EGMASAbilityAnswer::Rejected);
			TestEqual(TEXT("operation"), Answer->Value.OperationID, OpID);
			TestEqual(TEXT("no ability id"), Answer->Value.AbilityID, 0);

			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			Pair.DeliverAll();
			TestEqual(TEXT("cancelled"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
			TestEqual(TEXT("no end hook"), Client->EndAbilityEventCount, 0);
			TestEqual(TEXT("OnAbilityCancelled"), ClientRecorder->AbilityCancelledCount, 1);
		});

		It("answers Rejected when the server refuses in PreBeginAbility (cooldown)", [this]()
		{
			Pair.Server->SetCooldownForAbility(AbilityTagA(), 5.f);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);

			const TPair<bool, FGMASAbilitySyncMessage>* Answer = OnlyOutbox(EGMASAbilitySyncType::Answer);
			if (!TestNotNull(TEXT("exactly one answer"), Answer)) { return; }
			TestEqual(TEXT("rejected"), Answer->Value.Answer, EGMASAbilityAnswer::Rejected);
			TestEqual(TEXT("no ability id"), Answer->Value.AbilityID, 0);

			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			Pair.DeliverAll();
			TestEqual(TEXT("cancelled"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
		});

		It("answers Confirmed for an ability that began and ended inside its activation", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->bEndOnBegin = true;
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("server instance"), Server)) { return; }
			TestEqual(TEXT("ended naturally"), Server->EndAbilityEventCount, 1);
			TestTrue(TEXT("began"), Server->HasPassedActivationGates());
			const TPair<bool, FGMASAbilitySyncMessage>* Answer = OnlyOutbox(EGMASAbilitySyncType::Answer);
			if (!TestNotNull(TEXT("exactly one answer"), Answer)) { return; }
			TestEqual(TEXT("confirmed"), Answer->Value.Answer, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("ability id"), Answer->Value.AbilityID, IDFor(0));
		});

		It("holds an answer that arrives before the client ran the operation; Confirmed applies on creation", [this]()
		{
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			TestEqual(TEXT("held on the client"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 1);

			Pair.ActivateClientFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			TestTrue(TEXT("confirmed on creation"), Client->IsServerConfirmed());
			TestEqual(TEXT("running"), Client->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("held entry consumed"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 0);
		});

		It("holds an answer that arrives before the client ran the operation; Rejected cancels on creation", [this]()
		{
			Pair.Server->AddActiveTag(BlockA());
			Pair.Server->AddActiveTag(BlockB());
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			TestEqual(TEXT("held on the client"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 1);

			Pair.ActivateClientFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			TestEqual(TEXT("cancelled on creation"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
			TestEqual(TEXT("held entry consumed"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 0);
		});

		It("cancels the client instance when the server activated another candidate", [this]()
		{
			Pair.Server->AddActiveTag(BlockA());   // the server falls through to candidate 1
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestNotNull(TEXT("server ran candidate 1"), FGMASAbilitySyncPairHarness::FindByID(Pair.Server, IDFor(1)));

			const TPair<bool, FGMASAbilitySyncMessage>* Answer = OnlyOutbox(EGMASAbilitySyncType::Answer);
			if (!TestNotNull(TEXT("exactly one answer"), Answer)) { return; }
			TestEqual(TEXT("confirmed"), Answer->Value.Answer, EGMASAbilityAnswer::Confirmed);
			TestEqual(TEXT("candidate 1"), Answer->Value.CandidateIndex, 1);
			TestEqual(TEXT("candidate 1 id"), Answer->Value.AbilityID, IDFor(1));

			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client ran candidate 0"), Client)) { return; }
			Pair.DeliverAll();
			TestEqual(TEXT("client instance cancelled"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
		});

		It("times out after ServerConfirmTimeout of confirm clock when the answer is lost", [this]()
		{
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestTrue(TEXT("answer dropped"), Pair.DropNext(EGMASAbilitySyncType::Answer));
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }

			Pair.AdvanceClocks(1.5);
			Pair.Tick();
			TestEqual(TEXT("still running before the timeout"), Client->AbilityState, EAbilityState::Initialized);
			Pair.AdvanceClocks(1.0);
			Pair.Tick();
			TestEqual(TEXT("cancelled by the timeout"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
		});

		It("sends no second answer for a redelivered (already consumed) operation", [this]()
		{
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestEqual(TEXT("one answer"), Pair.CountOutbox(EGMASAbilitySyncType::Answer), 1);
		});

		It("sends nothing without a remote twin (standalone, listen host, AI)", [this]()
		{
			Pair.Server->bForceRemoteTwinForTest = false;
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestEqual(TEXT("server instance"), Pair.Server->GetActiveAbilities().Num(), 1);
			TestEqual(TEXT("no message"), Pair.Outbox.Num(), 0);
		});

		It("drops a held answer after AbilityAnswerHoldTime", [this]()
		{
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			TestEqual(TEXT("held"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 1);
			Pair.AdvanceClocks(2.0);
			Pair.Client->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("kept before the hold time"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 1);
			Pair.AdvanceClocks(3.5);
			Pair.Client->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("dropped after the hold time"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 0);
		});
	});

	Describe("Ended", [this]()
	{
		// Seconds past the server's peer-end grace (ServerOperationGraceSeconds, 1 s by default): a client
		// end is applied on the server only once its own moves had the time to end the instance in-move.
		constexpr double PastPeerEndGrace = 1.1;

		It("a server natural end runs EndAbility on the client (end hooks, not cancel hooks)", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Server->EndAbility();
			TestEqual(TEXT("kind recorded"), Server->GetEndKind(), EGMASAbilityEndKind::Natural);
			Pair.Tick();
			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended);
			if (!TestNotNull(TEXT("one Ended to the client"), Ended)) { return; }
			TestEqual(TEXT("id"), Ended->Value.AbilityID, IDFor(0));
			TestEqual(TEXT("natural"), Ended->Value.EndKind, EGMASAbilityEndKind::Natural);

			Pair.DeliverAll();
			TestEqual(TEXT("client ended"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("end hook"), Client->EndAbilityEventCount, 1);
			TestEqual(TEXT("no cancel hook"), Client->CancelAbilityEventCount, 0);
			TestEqual(TEXT("OnAbilityEnded"), ClientRecorder->AbilityEndedCount, 1);

			Pair.Tick();
			Pair.AncillaryTick();
			TestEqual(TEXT("the client does not echo a server end"), Pair.Outbox.Num(), 0);
		});

		It("a server cancel runs CancelAbility on the client", [this]()
		{
			AddExpectedMessagePlain(TEXT("[AbilityCut] Server cancelled an ability that was still active locally"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Server->CancelAbility();
			TestEqual(TEXT("kind recorded"), Server->GetEndKind(), EGMASAbilityEndKind::Cancelled);
			Pair.Tick();
			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended);
			if (!TestNotNull(TEXT("one Ended to the client"), Ended)) { return; }
			TestEqual(TEXT("cancelled"), Ended->Value.EndKind, EGMASAbilityEndKind::Cancelled);

			Pair.DeliverAll();
			TestEqual(TEXT("client ended"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
			TestEqual(TEXT("no end hook"), Client->EndAbilityEventCount, 0);
		});

		It("a client-only cancel cancels the server instance once the peer-end grace passed", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Client->CancelAbility();
			Pair.Tick();
			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended, /*bToServer=*/true);
			if (!TestNotNull(TEXT("one Ended to the server"), Ended)) { return; }
			TestEqual(TEXT("id"), Ended->Value.AbilityID, IDFor(0));
			TestEqual(TEXT("cancelled"), Ended->Value.EndKind, EGMASAbilityEndKind::Cancelled);

			Pair.DeliverAll();
			Pair.AncillaryTick();
			TestEqual(TEXT("server keeps running inside the grace (its own moves may still end it)"),
				Server->AbilityState, EAbilityState::Initialized);

			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			TestEqual(TEXT("server cancelled"), Server->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("server cancel hook"), Server->CancelAbilityEventCount, 1);
			TestEqual(TEXT("server no end hook"), Server->EndAbilityEventCount, 0);

			Pair.Tick();
			Pair.AncillaryTick();
			TestEqual(TEXT("the server does not echo a client end"), Pair.Outbox.Num(), 0);
		});

		It("both ending in-move: each side receives the other's Ended and changes nothing", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Client->EndAbility();
			Server->EndAbility();
			Pair.Tick();
			TestEqual(TEXT("one Ended each way"), CountOutbox(EGMASAbilitySyncType::Ended, true) + CountOutbox(EGMASAbilitySyncType::Ended, false), 2);
			Pair.DeliverAll();
			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			Pair.Tick();

			for (const UGMAS_TestAbility* Instance : { Client, Server })
			{
				TestEqual(TEXT("one end hook"), Instance->EndAbilityEventCount, 1);
				TestEqual(TEXT("no cancel hook"), Instance->CancelAbilityEventCount, 0);
			}
			TestEqual(TEXT("nothing held"), Pair.Client->GetHeldSyncMessagesForTest().Num() + Pair.Server->GetHeldSyncMessagesForTest().Num(), 0);
			TestEqual(TEXT("nothing further sent"), Pair.Outbox.Num(), 0);
		});

		It("a server end that arrives before the client instance exists is held and ends it on creation", [this]()
		{
			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("server"), Server)) { return; }
			Server->EndAbility();
			Pair.Tick();
			Pair.DeliverAll();   // the answer, then the end: both held
			TestEqual(TEXT("held on the client"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 2);

			Pair.ActivateClientFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client"), Client)) { return; }
			TestEqual(TEXT("ended on creation"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("end hook"), Client->EndAbilityEventCount, 1);
			TestEqual(TEXT("no cancel hook"), Client->CancelAbilityEventCount, 0);
			TestEqual(TEXT("held consumed"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 0);

			Pair.Tick();
			TestEqual(TEXT("no echo"), Pair.Outbox.Num(), 0);
		});

		It("a client end that arrives before the server ran the operation is held and ends the server instance", [this]()
		{
			Pair.ActivateClientFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client"), Client)) { return; }
			Client->CancelAbility();
			Pair.Tick();
			Pair.DeliverAll();
			TestEqual(TEXT("held on the server"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 1);

			Pair.ActivateServerFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("server"), Server)) { return; }
			TestEqual(TEXT("held consumed"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 0);
			Pair.DeliverAll();   // the answer: the client's instance already ended, nothing to do

			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			TestEqual(TEXT("server cancelled"), Server->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("server cancel hook"), Server->CancelAbilityEventCount, 1);
			Pair.Tick();
			TestEqual(TEXT("no echo"), Pair.Outbox.Num(), 0);
		});

		It("a server-only instance sends nothing and is never ended by sync", [this]()
		{
			TestTrue(TEXT("activated"), Pair.Server->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
			UGMAS_TestAbility* ServerOnly = nullptr;
			for (const TPair<int, UGMCAbility*>& Entry : Pair.Server->GetActiveAbilities())
			{
				ServerOnly = Cast<UGMAS_TestAbility>(Entry.Value);
			}
			if (!TestNotNull(TEXT("server-only instance"), ServerOnly)) { return; }

			FGMASAbilitySyncMessage Ended;
			Ended.Type = EGMASAbilitySyncType::Ended;
			Ended.AbilityID = ServerOnly->GetAbilityID();
			Ended.EndKind = EGMASAbilityEndKind::Cancelled;
			Pair.Server->ReceiveAbilitySyncForTest(Ended);
			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			TestEqual(TEXT("an end naming it changes nothing"), ServerOnly->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("not held either"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 0);

			ServerOnly->EndAbility();
			Pair.Tick();
			Pair.AncillaryTick();
			TestEqual(TEXT("its end sends nothing"), Pair.Outbox.Num(), 0);
		});

		It("a confirm timeout reports the end exactly once", [this]()
		{
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DropNext(EGMASAbilitySyncType::Answer);
			Pair.AdvanceClocks(2.5);
			Pair.Tick();
			Pair.Tick();
			Pair.AncillaryTick();
			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended, /*bToServer=*/true);
			if (!TestNotNull(TEXT("exactly one Ended to the server"), Ended)) { return; }
			TestEqual(TEXT("cancelled"), Ended->Value.EndKind, EGMASAbilityEndKind::Cancelled);
			TestEqual(TEXT("id"), Ended->Value.AbilityID, IDFor(0));
		});

		It("a client end inside a replayed move is sent after the replay, once", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client"), Client)) { return; }

			Pair.Client->bForceReplayingForTest = true;
			Client->CancelAbility();
			Pair.Client->CleanupStaleAbilitiesForTest();
			TestEqual(TEXT("nothing sent during the replay"), Pair.Outbox.Num(), 0);
			Pair.Client->bForceReplayingForTest = false;

			Pair.Client->GenAncillaryTick(0.f, false);
			Pair.Client->CleanupStaleAbilitiesForTest();
			TestEqual(TEXT("sent once afterwards"), CountOutbox(EGMASAbilitySyncType::Ended, true), 1);
		});

		It("a candidate mismatch also reports the server's instance, which the server then cancels", [this]()
		{
			Pair.Server->AddActiveTag(BlockA());   // the server falls through to candidate 1
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			UGMCAbility* ServerB = FGMASAbilitySyncPairHarness::FindByID(Pair.Server, IDFor(1));
			if (!TestNotNull(TEXT("server candidate 1"), ServerB)) { return; }
			Pair.DeliverAll();
			Pair.Tick();

			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended, /*bToServer=*/true);
			if (!TestNotNull(TEXT("one Ended to the server"), Ended)) { return; }
			TestEqual(TEXT("names the server's instance"), Ended->Value.AbilityID, IDFor(1));
			TestEqual(TEXT("cancelled"), Ended->Value.EndKind, EGMASAbilityEndKind::Cancelled);

			Pair.DeliverAll();
			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			TestEqual(TEXT("server instance cancelled"), ServerB->AbilityState, EAbilityState::Ended);
		});

		It("a Confirmed answer for an operation the client ran without an instance reports the server's instance", [this]()
		{
			Pair.Client->AddActiveTag(BlockA());
			Pair.Client->AddActiveTag(BlockB());   // the client refuses every candidate
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			TestEqual(TEXT("no client instance"), Pair.Client->GetActiveAbilities().Num(), 0);
			Pair.DeliverAll();
			Pair.Tick();

			const TPair<bool, FGMASAbilitySyncMessage>* Ended = OnlyOutbox(EGMASAbilitySyncType::Ended, /*bToServer=*/true);
			if (!TestNotNull(TEXT("one Ended to the server"), Ended)) { return; }
			TestEqual(TEXT("names the server's instance"), Ended->Value.AbilityID, IDFor(0));
			TestEqual(TEXT("nothing held"), Pair.Client->GetHeldSyncMessagesForTest().Num(), 0);
		});
	});

	Describe("Digest", [this]()
	{
		// Defaults: AbilityReconcileMinAge 1 s, AbilityDigestInterval 1 s, ServerOperationGraceSeconds 1 s.
		constexpr double PastMinAge = 1.1;
		constexpr double PastPeerEndGrace = 1.1;

		It("catches a server-only orphan: the digest goes out, the client reports it, the server cancels it", [this]()
		{
			AddExpectedMessagePlain(TEXT("[AbilityReconcile] server-only"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Client->CancelAbility();
			Pair.Tick();
			TestTrue(TEXT("the client's Ended is lost"), Pair.DropNext(EGMASAbilitySyncType::Ended));
			TestEqual(TEXT("server still running"), Server->AbilityState, EAbilityState::Initialized);

			Pair.AdvanceClocks(PastMinAge);
			Pair.AncillaryTick();
			const TPair<bool, FGMASAbilitySyncMessage>* Digest = OnlyOutbox(EGMASAbilitySyncType::Digest);
			if (!TestNotNull(TEXT("one digest to the client"), Digest)) { return; }
			TestTrue(TEXT("lists the server's instance"), Digest->Value.DigestIDs.Num() == 1 && Digest->Value.DigestIDs[0] == IDFor(0));

			Pair.DeliverNext();   // only the digest
			const TPair<bool, FGMASAbilitySyncMessage>* Report = OnlyOutbox(EGMASAbilitySyncType::Ended, /*bToServer=*/true);
			if (!TestNotNull(TEXT("the client reports it ended"), Report)) { return; }
			TestEqual(TEXT("id"), Report->Value.AbilityID, IDFor(0));
			TestEqual(TEXT("cancelled"), Report->Value.EndKind, EGMASAbilityEndKind::Cancelled);

			Pair.DeliverAll();
			Pair.AdvanceClocks(PastPeerEndGrace);
			Pair.AncillaryTick();
			TestEqual(TEXT("server cancelled"), Server->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("server cancel hook"), Server->CancelAbilityEventCount, 1);
			Pair.DeliverAll();
			TestEqual(TEXT("nothing further reported"), CountOutbox(EGMASAbilitySyncType::Ended, true), 0);
		});

		It("does not report an instance the client ended moments ago (its Ended may still be in flight)", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			if (!TestNotNull(TEXT("client"), Client)) { return; }
			Pair.AdvanceClocks(PastMinAge);
			Client->CancelAbility();
			Pair.Tick();
			Pair.DropNext(EGMASAbilitySyncType::Ended);   // stands in for an Ended still in flight

			FGMASAbilitySyncMessage Digest;
			Digest.Type = EGMASAbilitySyncType::Digest;
			Digest.DigestIDs = { IDFor(0) };
			Pair.Client->ReceiveAbilitySyncForTest(Digest);
			TestEqual(TEXT("no report"), Pair.Outbox.Num(), 0);
		});

		It("ends a client-only orphan once it is MinAge + Interval old, not before", [this]()
		{
			AddExpectedMessagePlain(TEXT("[AbilityReconcile] client-only"),
				ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Server->EndAbility();
			Pair.Tick();
			TestTrue(TEXT("the server's Ended is lost"), Pair.DropNext(EGMASAbilitySyncType::Ended));

			// A digest without the instance while it is younger than MinAge + Interval: kept (margin for latency).
			Pair.AdvanceClocks(1.5);
			FGMASAbilitySyncMessage Empty;
			Empty.Type = EGMASAbilitySyncType::Digest;
			Pair.Client->ReceiveAbilitySyncForTest(Empty);
			TestEqual(TEXT("kept before 2 s"), Client->AbilityState, EAbilityState::Initialized);

			// The server has nothing old to digest; the client asks, the server answers with an empty digest.
			Pair.AdvanceClocks(0.6);
			Pair.AncillaryTick();
			TestEqual(TEXT("no request right after a digest"), CountOutbox(EGMASAbilitySyncType::RequestDigest, true), 0);
			Pair.AdvanceClocks(1.5);
			Pair.AncillaryTick();
			TestEqual(TEXT("request sent"), CountOutbox(EGMASAbilitySyncType::RequestDigest, true), 1);
			Pair.DeliverAll();
			TestEqual(TEXT("cancelled once 2 s old and missing from the digest"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Client->CancelAbilityEventCount, 1);
			TestEqual(TEXT("no end hook"), Client->EndAbilityEventCount, 0);
		});

		It("sends no digest while no covered instance is MinAge old (server-only instances never count)", [this]()
		{
			TestTrue(TEXT("server-only"), Pair.Server->TryActivateAbility(UGMAS_TestAbilityB::StaticClass()));
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			Pair.AdvanceClocks(0.5);
			Pair.AncillaryTick();
			TestEqual(TEXT("nothing sent at 0.5 s"), Pair.Outbox.Num(), 0);

			ClientA()->CancelAbility();
			ServerA()->CancelAbility();
			Pair.Tick();
			Pair.DeliverAll();
			Pair.AdvanceClocks(PastMinAge + PastPeerEndGrace);
			Pair.AncillaryTick();
			Pair.DeliverAll();
			Pair.AdvanceClocks(PastMinAge);
			Pair.AncillaryTick();
			TestEqual(TEXT("no digest for a server-only instance"), CountOutbox(EGMASAbilitySyncType::Digest, false), 0);
		});

		It("sends one digest per interval, listing every covered live instance", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			Pair.AdvanceClocks(PastMinAge);
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID - 1);   // young
			Pair.DeliverAll();
			Pair.Server->GenAncillaryTick(0.f, false);
			Pair.Server->GenAncillaryTick(0.f, false);
			const TPair<bool, FGMASAbilitySyncMessage>* Digest = OnlyOutbox(EGMASAbilitySyncType::Digest);
			if (!TestNotNull(TEXT("one digest in the interval"), Digest)) { return; }
			TestEqual(TEXT("both instances, any age"), Digest->Value.DigestIDs.Num(), 2);
			Pair.DeliverAll();
			TestEqual(TEXT("both still running on the client"), Pair.Client->GetActiveAbilities().Num(), 2);
			TestEqual(TEXT("nothing reported"), Pair.Outbox.Num(), 0);

			Pair.AdvanceClocks(1.05);
			Pair.Server->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("next interval: next digest"), CountOutbox(EGMASAbilitySyncType::Digest, false), 1);
		});

		It("requests a digest after 2 intervals without one; the server replies at once", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();

			Pair.AdvanceClocks(PastMinAge);
			Pair.Client->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("no request before 2 intervals"), Pair.Outbox.Num(), 0);

			Pair.AdvanceClocks(1.0);
			Pair.Client->GenAncillaryTick(0.f, false);
			Pair.Client->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("one request"), CountOutbox(EGMASAbilitySyncType::RequestDigest, true), 1);
			Pair.DeliverAll();   // the server replies; the client consumes the reply
			TestEqual(TEXT("reply delivered, nothing left"), Pair.Outbox.Num(), 0);
			TestNotNull(TEXT("client instance kept"), ClientA());
			TestEqual(TEXT("client running"), ClientA() ? ClientA()->AbilityState : EAbilityState::Ended, EAbilityState::Initialized);

			Pair.AdvanceClocks(1.0);
			Pair.Client->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("no request right after a digest"), Pair.Outbox.Num(), 0);
		});

		It("leaves a client-authorized instance running for 5 s (no timeout, no digest end)", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->bActivateOnMovementTick = false;
			for (UGMC_AbilitySystemComponent* Side : { Pair.Client, Pair.Server })
			{
				Side->ClientAuthorizedAbilities.Add(UGMAS_TestAbility::StaticClass());
			}
			TestTrue(TEXT("client activated"), Pair.Client->TryActivateClientAuthAbilityForTest(
				UGMAS_TestAbility::StaticClass(), FGMASAbilitySyncPairHarness::InputTag(), nullptr));
			UGMCAbility* Client = nullptr;
			for (const TPair<int, UGMCAbility*>& Entry : Pair.Client->GetActiveAbilities()) { Client = Entry.Value; }
			if (!TestNotNull(TEXT("client instance"), Client)) { return; }
			const int32 ClientAuthOp = Client->GetSourceOperationID();
			// The orphan stub reports NM_Standalone: drop the queued client operation as a sent move would.
			Pair.Client->GetBoundQueueV2ForTest().RemovePayloadByID(ClientAuthOp);
			Pair.Client->GetBoundQueueV2ForTest().ClientQueuedOperations.Reset();

			FGMASBoundQueueV2ClientAuthAbilityActivationOperation Op;
			Op.OperationID = ClientAuthOp;
			Op.AbilityClass = UGMAS_TestAbility::StaticClass();
			Op.InputTag = FGMASAbilitySyncPairHarness::InputTag();
			Pair.Server->ServerProcessOperationForTest(FInstancedStruct::Make(Op), false);
			UGMCAbility* Server = FGMASAbilitySyncPairHarness::FindByID(Pair.Server, Client->GetAbilityID());
			if (!TestNotNull(TEXT("server twin"), Server)) { return; }
			TestTrue(TEXT("the answer is lost"), Pair.DropNext(EGMASAbilitySyncType::Answer));

			for (int32 Second = 0; Second < 5; ++Second)
			{
				Pair.AdvanceClocks(1.0);
				Pair.AncillaryTick();
				Pair.DeliverAll();
				Pair.Tick();
			}
			TestEqual(TEXT("client running"), Client->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("server running"), Server->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("nothing pending"), Pair.Outbox.Num(), 0);
		});
	});

	Describe("Reconnect", [this]()
	{
		It("a reconnect cancels every covered instance and clears the held table, sending nothing", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("server"), Server)) { return; }
			TestTrue(TEXT("server-only"), Pair.Server->TryActivateAbility(UGMAS_TestAbilityB::StaticClass()));

			FGMASAbilitySyncMessage Early;   // a client end for an instance the server has not created
			Early.Type = EGMASAbilitySyncType::Ended;
			Early.AbilityID = IDFor(1) + 1000;
			Pair.Server->ReceiveAbilitySyncForTest(Early);
			TestEqual(TEXT("held before the reset"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 1);

			// The first request is the owning connection's own, right after login: its instances are kept.
			Pair.Server->Server_RequestActiveEffectsSnapshot_Implementation();
			TestEqual(TEXT("kept on the first connection's request"), Server->AbilityState, EAbilityState::Initialized);
			TestEqual(TEXT("held kept on the first request"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 1);

			// A later request is a new connection taking the pawn over.
			Pair.Server->Server_RequestActiveEffectsSnapshot_Implementation();
			TestEqual(TEXT("covered instance cancelled"), Server->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("cancel hook"), Server->CancelAbilityEventCount, 1);
			TestNull(TEXT("and gone, so a new connection's ids cannot meet it"), ServerA());
			TestEqual(TEXT("held table cleared"), Pair.Server->GetHeldSyncMessagesForTest().Num(), 0);
			TestEqual(TEXT("server-only instance kept"), Pair.Server->GetActiveAbilities().Num(), 1);

			// The new client's first move overwrites the bound operation slot that still holds the old -3.
			Pair.Server->SeedBoundQueueOperationDataForTest(0);
			Pair.Tick();
			Pair.AdvanceClocks(1.1);
			Pair.Server->GenAncillaryTick(0.f, false);
			TestEqual(TEXT("nothing sent to the new connection"), Pair.Outbox.Num(), 0);
		});
	});

	Describe("FaultInjection", [this]()
	{
		It("dropped answers (DropAbilityAnswers cvar): the pair converges through the timeout and the reported end", [this]()
		{
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			if (!TestTrue(TEXT("cvar exists"), SetDebugCVar(TEXT("GMAS.Debug.DropAbilityAnswers"), 1))) { return; }
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			SetDebugCVar(TEXT("GMAS.Debug.DropAbilityAnswers"), 0);
			TestEqual(TEXT("answer dropped"), Pair.CountOutbox(EGMASAbilitySyncType::Answer), 0);
			UGMAS_TestAbility* Client = ClientA();
			UGMAS_TestAbility* Server = ServerA();
			if (!TestNotNull(TEXT("client"), Client) || !TestNotNull(TEXT("server"), Server)) { return; }

			Pair.AdvanceClocks(2.5);
			Pair.Tick();
			TestEqual(TEXT("client timed out"), Client->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("its end reported"), CountOutbox(EGMASAbilitySyncType::Ended, true), 1);
			Pair.DeliverAll();
			Pair.AdvanceClocks(1.1);
			Pair.AncillaryTick();
			Pair.DeliverAll();
			TestEqual(TEXT("server cancelled"), Server->AbilityState, EAbilityState::Ended);
			TestEqual(TEXT("server cancel hook"), Server->CancelAbilityEventCount, 1);
		});

		It("the DropAbilityEnds cvar drops ends in both directions", [this]()
		{
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			Pair.DeliverAll();
			if (!TestTrue(TEXT("cvar exists"), SetDebugCVar(TEXT("GMAS.Debug.DropAbilityEnds"), 1))) { return; }
			ClientA()->EndAbility();
			ServerA()->EndAbility();
			Pair.Tick();
			Pair.AncillaryTick();
			TestEqual(TEXT("no Ended sent"), Pair.CountOutbox(EGMASAbilitySyncType::Ended), 0);
		});

		It("the DelayAbilityMessagesMs cvar holds sends on the sender and releases them in order", [this]()
		{
			if (!TestTrue(TEXT("cvar exists"), SetDebugCVar(TEXT("GMAS.Debug.DelayAbilityMessagesMs"), 500))) { return; }
			Pair.ActivateBothFromOperation(FGMASAbilitySyncPairHarness::InputTag(), OpID);
			ServerA()->EndAbility();
			Pair.Tick();
			TestEqual(TEXT("nothing sent yet"), Pair.Outbox.Num(), 0);
			Pair.AdvanceClocks(0.3);
			Pair.AncillaryTick();
			TestEqual(TEXT("still held at 0.3 s"), Pair.Outbox.Num(), 0);
			Pair.AdvanceClocks(0.3);
			Pair.AncillaryTick();
			if (!TestEqual(TEXT("released at 0.6 s"), Pair.Outbox.Num(), 2)) { return; }
			TestEqual(TEXT("the answer first"), Pair.Outbox[0].Value.Type, EGMASAbilitySyncType::Answer);
			TestEqual(TEXT("then the end"), Pair.Outbox[1].Value.Type, EGMASAbilitySyncType::Ended);
		});
	});
}

#endif
