// Ability end paths: the natural end (EndAbility: end event, OnAbilityEnded, chain window) against
// the cancel (CancelAbility: FinishEndAbility only) on every path that interrupts an ability from
// outside: CancelAbilitiesByTag / ByQuery, an effect's CancelAbilityOnActivation and the client
// confirm timeout. Headless UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent harness, no world.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Ability/GMCAbility.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestEventRecorder.h"
#include "GMAS_TestHelpers.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAbilityEndSpec,
	"GMAS.Unit.AbilityEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMAS_TestEventRecorder*        Recorder    = nullptr;

	// Effects a case roots for its lifetime; released by TeardownHarness.
	TArray<UGMCAbilityEffect*> Kept;

	FGameplayTag SwingTag;        // the test ability's AbilityTag
	FGameplayTag WindowTag;       // its chain window
	FGameplayTag StunTag;         // EffectTag of the cancelling effect
	FGameplayTag DefinitionTag;   // AbilityDefinition entry matched by the queries

	void SetupHarness();
	void TeardownHarness();

	// The first non-Ended test ability instance, or nullptr.
	UGMAS_TestAbility* FirstLiveAbility() const;
	// The first Ended (unpurged) test ability instance, or nullptr.
	UGMAS_TestAbility* FirstEndedAbility() const;

END_DEFINE_SPEC(FGMASAbilityEndSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASAbilityEndSpec::SetupHarness()
{
	static FNativeGameplayTag SSwingTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Ability.Swing"), TEXT("Ability tag for ability-end tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SWindowTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Chain.Window"), TEXT("Chain window tag for ability-end tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SStunTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Effect.Stun"), TEXT("Cancelling effect tag for ability-end tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SDefinitionTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Definition.Melee"), TEXT("AbilityDefinition tag for ability-end tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	SwingTag      = SSwingTag.GetTag();
	WindowTag     = SWindowTag.GetTag();
	StunTag       = SStunTag.GetTag();
	DefinitionTag = SDefinitionTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(GMASTest::StableActionTimer);

	Recorder = NewObject<UGMAS_TestEventRecorder>(GetTransientPackage());
	Recorder->AddToRoot();
	Recorder->Bind(AbilityComp);

	// The test ability opens a chain window on its natural end; several instances may run at once.
	UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
	CDO->AbilityTag              = SwingTag;
	CDO->ChainWindowTag          = WindowTag;
	CDO->ChainWindowDuration     = 1.f;
	CDO->bAllowMultipleInstances = true;
	CDO->CooldownTime            = 0.f;
}

void FGMASAbilityEndSpec::TeardownHarness()
{
	UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
	CDO->AbilityTag              = FGameplayTag();
	CDO->ChainWindowTag          = FGameplayTag();
	CDO->ChainWindowDuration     = 0.f;
	CDO->bAllowMultipleInstances = false;
	CDO->CooldownTime            = 0.f;
	CDO->AbilityDefinition       = FGameplayTagContainer();
	CDO->ApplyEffectOnEnd.Empty();
	CDO->bCommitCostOnBegin      = false;

	for (UGMCAbilityEffect* Effect : Kept) { Effect->RemoveFromRoot(); }
	Kept.Reset();

	Recorder->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	Recorder = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

UGMAS_TestAbility* FGMASAbilityEndSpec::FirstLiveAbility() const
{
	for (const auto& Pair : AbilityComp->GetActiveAbilities())
	{
		UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(Pair.Value);
		if (Ability && Ability->AbilityState != EAbilityState::Ended)
		{
			return Ability;
		}
	}
	return nullptr;
}

UGMAS_TestAbility* FGMASAbilityEndSpec::FirstEndedAbility() const
{
	for (const auto& Pair : AbilityComp->GetActiveAbilities())
	{
		UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(Pair.Value);
		if (Ability && Ability->AbilityState == EAbilityState::Ended)
		{
			return Ability;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASAbilityEndSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("End paths", [this]()
	{
		It("EndAbilitiesByTag is the natural end: end event, OnAbilityEnded, chain window", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			AbilityComp->EndAbilitiesByTag(SwingTag);
			TestEqual("end event once", Ability->EndAbilityEventCount, 1);
			TestEqual("OnAbilityEnded once", Recorder->AbilityEndedCount, 1);
			TestEqual("no cancel event", Ability->CancelAbilityEventCount, 0);
			TestEqual("no OnAbilityCancelled", Recorder->AbilityCancelledCount, 0);
			TestTrue("chain window granted", AbilityComp->HasActiveTag(WindowTag));
		});

		It("CancelAbilitiesByTag is abnormal: no end event, no OnAbilityEnded, no window", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("one cancelled", AbilityComp->CancelAbilitiesByTag(SwingTag), 1);
			TestEqual("state Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestEqual("no OnAbilityEnded", Recorder->AbilityEndedCount, 0);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestFalse("no chain window", AbilityComp->HasActiveTag(WindowTag));
		});

		It("an effect's CancelAbilityOnActivation cancels", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = StunTag; Data.CancelAbilityOnActivation.AddTag(SwingTag);
			AbilityComp->ApplyAbilityEffect(Effect, Data);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestFalse("no window", AbilityComp->HasActiveTag(WindowTag));
		});

		It("CancelAbilitiesByQuery cancels on the next tick; EndAbilitiesByQuery ends naturally", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->AbilityDefinition = FGameplayTagContainer(DefinitionTag);
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* A = FirstLiveAbility();
			if (!TestNotNull("A", A)) { return; }
			const FGameplayTagQuery Query = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(DefinitionTag));
			TestEqual("flagged", AbilityComp->CancelAbilitiesByQuery(Query), 1);
			AbilityComp->TickActiveAbilitiesForTest(0.f);
			TestEqual("Ended on tick", A->AbilityState, EAbilityState::Ended);
			TestEqual("no end event", A->EndAbilityEventCount, 0);
			TestEqual("no OnAbilityEnded", Recorder->AbilityEndedCount, 0);
			TestEqual("cancel event once", A->CancelAbilityEventCount, 1);
			AbilityComp->CleanupStaleAbilitiesForTest();

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* B = FirstLiveAbility();
			if (!TestNotNull("B", B)) { return; }
			AbilityComp->EndAbilitiesByQuery(Query);
			AbilityComp->TickActiveAbilitiesForTest(0.f);
			TestEqual("end event on the natural sibling", B->EndAbilityEventCount, 1);
		});

		It("the confirm timeout cancels", [this]()
		{
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = false;
			AbilityComp->SetActionTimerForTest(1.0);
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			AbilityComp->SetActionTimerForTest(3.5);
			AbilityComp->TickActiveAbilitiesForTest(0.5f);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestFalse("no window", AbilityComp->HasActiveTag(WindowTag));
		});

		// The declared effect's end runs inside the ability's own unwind and targets the ability
		// again through CancelAbilityOnEnd; the unwind, the cancel hooks and the effect's chain
		// hook must all run once.
		It("a declared effect whose CancelAbilityOnEnd names its own ability does not unwind twice", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = StunTag;
			Data.CancelAbilityOnEnd.AddTag(SwingTag);
			Data.ApplyEffectOnEnd.Add(UGMCAbilityEffect::StaticClass());
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { return; }
			Ability->DeclareEffect(Applied->EffectData.EffectID, EGMCAbilityEffectQueueType::Predicted);
			const int EffectsBefore = AbilityComp->GetActiveEffects().Num();

			TestEqual("one cancelled", AbilityComp->CancelAbilitiesByTag(SwingTag), 1);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			TestTrue("declared effect ended", Applied->bCompleted);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestEqual("the effect's chain hook applied exactly one effect", AbilityComp->GetActiveEffects().Num(), EffectsBefore + 1);
		});

		// PreBeginAbility refuses through CancelAbility, but a refused press is not an interruption:
		// the dead-born instance ends silently.
		It("a refused activation fires no cancel hooks", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->CooldownTime = 5.f;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());   // begins, commits the cooldown
			UGMAS_TestAbility* First = FirstLiveAbility();
			if (!TestNotNull("first instance", First)) { return; }
			TestTrue("on cooldown", AbilityComp->GetCooldownForAbility(SwingTag) > 0.f);
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());   // refused by the cooldown

			UGMAS_TestAbility* DeadBorn = FirstEndedAbility();
			if (!TestNotNull("dead-born instance registered as Ended", DeadBorn)) { return; }
			TestEqual("never began", DeadBorn->BeginAbilityEventCount, 0);
			TestEqual("no cancel event", DeadBorn->CancelAbilityEventCount, 0);
			TestEqual("no OnAbilityCancelled", Recorder->AbilityCancelledCount, 0);
			TestEqual("first instance still live", First->AbilityState, EAbilityState::Initialized);
		});

		// FinishEndAbility runs for a dead-born instance too; its ApplyEffectOnEnd / RemoveEffectOnEnd
		// are for an ability that ran, not for a refused press.
		It("a refused activation applies no end effects", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->CooldownTime = 5.f;
			GetMutableDefault<UGMAS_TestAbility>()->ChainWindowTag = FGameplayTag();   // no window effect: the count below isolates ApplyEffectOnEnd
			GetMutableDefault<UGMAS_TestAbility>()->ApplyEffectOnEnd.Add(UGMCAbilityEffect::StaticClass());
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());   // begins, commits the cooldown
			if (!TestNotNull("first instance", FirstLiveAbility())) { return; }
			TestTrue("on cooldown", AbilityComp->GetCooldownForAbility(SwingTag) > 0.f);
			const int EffectsBefore = AbilityComp->GetActiveEffects().Num();

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());   // refused by the cooldown
			if (!TestNotNull("dead-born instance registered as Ended", FirstEndedAbility())) { return; }
			TestEqual("no end effect for the refused press", AbilityComp->GetActiveEffects().Num(), EffectsBefore);

			TestEqual("one ended", AbilityComp->EndAbilitiesByTag(SwingTag), 1);
			TestEqual("the natural end still applies its end effect", AbilityComp->GetActiveEffects().Num(), EffectsBefore + 1);
		});

		// PreBeginAbility marks the ability Initialized before OnAbilityActivated, so a listener's cancel
		// is the cancel of a begun ability and the activation stops there: no cooldown, no
		// BeginAbilityEvent, and the instance is an ordinary Ended one (purged, never blocking).
		It("a cancel from an OnAbilityActivated listener ends the ability", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;
			GetMutableDefault<UGMAS_TestAbility>()->CooldownTime = 5.f;   // a committed cooldown would refuse the second activation
			Recorder->CancelAbilityOnActivatedTag = SwingTag;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstEndedAbility();
			if (!TestNotNull("instance ended by the listener", Ability)) { return; }
			TestEqual("activated event once", Recorder->AbilityActivatedCount, 1);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestEqual("BeginAbilityEvent never ran", Ability->BeginAbilityEventCount, 0);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestFalse("no chain window", AbilityComp->HasActiveTag(WindowTag));
			TestEqual("no cooldown committed", AbilityComp->GetCooldownForAbility(SwingTag), 0.f);
			TestNull("no live instance", FirstLiveAbility());

			Recorder->CancelAbilityOnActivatedTag = FGameplayTag();
			const bool bSecond = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("second activation not blocked by the ended instance", bSecond);
			TestEqual("one live instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
			UGMAS_TestAbility* Second = FirstLiveAbility();
			if (TestNotNull("second instance", Second)) { TestEqual("second began", Second->BeginAbilityEventCount, 1); }

			const int EndedID = Ability->GetAbilityID();
			AbilityComp->CleanupStaleAbilitiesForTest();
			TestFalse("ended instance purged", AbilityComp->GetActiveAbilities().Contains(EndedID));
		});

		// The chain window is applied before FinishEndAbility; a listener on it that cancels the ending
		// ability hits the entry latch: the natural end runs to completion and the cancel is ignored.
		It("a cancel from the chain window's OnEffectApplied listener during EndAbility is ignored", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			Recorder->CancelAbilityOnEffectAppliedTag = SwingTag;
			TestEqual("one ended", AbilityComp->EndAbilitiesByTag(SwingTag), 1);
			TestEqual("window effect applied", Recorder->AppliedIDs.Num(), 1);
			TestEqual("state Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("end event once", Ability->EndAbilityEventCount, 1);
			TestEqual("OnAbilityEnded once", Recorder->AbilityEndedCount, 1);
			TestEqual("no cancel event", Ability->CancelAbilityEventCount, 0);
			TestEqual("no OnAbilityCancelled", Recorder->AbilityCancelledCount, 0);
			TestTrue("chain window granted", AbilityComp->HasActiveTag(WindowTag));
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
