// Ability end paths: the natural end (EndAbility: end event, OnAbilityEnded, chain window) against
// the cancel (CancelAbility: FinishEndAbility only) on every path that interrupts an ability from
// outside: CancelAbilitiesByTag / ByQuery, an effect's CancelAbilityOnActivation, the client
// confirm timeout and the server's Rejected answer. Also the AbilityCost gate (refusal before BeginAbility, a committed Instant cost,
// a declared lasting cost, GetAbilityCostValues) and the instance defaults an activation inherits
// from its CDO. Headless UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent harness, no world.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Ability/GMCAbility.h"
#include "Attributes/GMCAttributesData.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestCostEffect.h"
#include "UGMAS_TestEventRecorder.h"
#include "GMAS_TestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FGMASAbilityEndSpec,
	"GMAS.Unit.AbilityEnd",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMAS_TestEventRecorder*        Recorder    = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;

	// Effects a case roots for its lifetime; released by TeardownHarness.
	TArray<UGMCAbilityEffect*> Kept;

	FGameplayTag SwingTag;        // the test ability's AbilityTag
	FGameplayTag WindowTag;       // its chain window
	FGameplayTag StunTag;         // EffectTag of the cancelling effect
	FGameplayTag DefinitionTag;   // AbilityDefinition entry matched by the queries
	FGameplayTag ManaTag;         // bound attribute the cost cases spend, [0, 100], default 50
	FGameplayTag CostTag;         // EffectTag of the cost effect

	// Project grace default, restored by TeardownHarness (the networked cost case changes it).
	float SavedDefaultClientGraceTime = 0.f;

	void SetupHarness();
	void TeardownHarness();

	// The first non-Ended test ability instance, or nullptr.
	UGMAS_TestAbility* FirstLiveAbility() const;
	// The first Ended (unpurged) test ability instance, or nullptr.
	UGMAS_TestAbility* FirstEndedAbility() const;
	// The first registered ability instance whether or not it ended, or nullptr.
	UGMCAbility* AnyAbility() const;

	// Add <Amount> to Mana (AMT_Value).
	FGMCAttributeModifier MakeManaMod(float Amount) const;
	// Set Mana to <Target> (AMT_Value).
	FGMCAttributeModifier MakeManaSetMod(float Target) const;
	// Bring Mana to <Target> through an Instant effect, processed now.
	void SetMana(float Target);

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
	static FNativeGameplayTag SManaTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Attribute.Mana"), TEXT("Mana for the ability cost tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SCostTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.End.Effect.Cost"), TEXT("Cost effect tag for the ability cost tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	SwingTag      = SSwingTag.GetTag();
	WindowTag     = SWindowTag.GetTag();
	StunTag       = SStunTag.GetTag();
	DefinitionTag = SDefinitionTag.GetTag();
	ManaTag       = SManaTag.GetTag();
	CostTag       = SCostTag.GetTag();

	SavedDefaultClientGraceTime = GetDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime;

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	// One bound attribute for the cost cases; registered before BindReplicationData builds the set.
	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();
	{
		FAttributeData Mana;
		Mana.AttributeTag = ManaTag;
		Mana.DefaultValue = 50.f;
		Mana.Clamp.Min    = 0.f;
		Mana.Clamp.Max    = 100.f;
		Mana.bGMCBound    = true;
		AttrData->AttributeData.Add(Mana);
	}
	AbilityComp->AttributeDataAssets.Add(AttrData);

	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(GMASTest::StableActionTimer);
	AbilityComp->SilenceEffectIDWrapReportForTest();   // negative clock: ids wrap into their ranges

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
	CDO->AbilityTag                  = FGameplayTag();
	CDO->ChainWindowTag              = FGameplayTag();
	CDO->ChainWindowDuration         = 0.f;
	CDO->bAllowMultipleInstances     = false;
	CDO->CooldownTime                = 0.f;
	CDO->bApplyCooldownAtAbilityBegin = true;
	CDO->AbilityDefinition           = FGameplayTagContainer();
	CDO->ApplyEffectOnEnd.Empty();
	CDO->AbilityCost                 = nullptr;
	CDO->bCommitCostOnBegin          = false;
	GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = FGMCAbilityEffectData();
	GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = SavedDefaultClientGraceTime;
	AbilityComp->bForceNetworkedForTest = false;

	for (UGMCAbilityEffect* Effect : Kept) { Effect->RemoveFromRoot(); }
	Kept.Reset();

	Recorder->RemoveFromRoot();
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	Recorder = nullptr; AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
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

UGMCAbility* FGMASAbilityEndSpec::AnyAbility() const
{
	for (const auto& Pair : AbilityComp->GetActiveAbilities())
	{
		if (Pair.Value) { return Pair.Value; }
	}
	return nullptr;
}

FGMCAttributeModifier FGMASAbilityEndSpec::MakeManaMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = ManaTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

FGMCAttributeModifier FGMASAbilityEndSpec::MakeManaSetMod(float Target) const
{
	FGMCAttributeModifier Mod = MakeManaMod(Target);
	Mod.Op = EModifierType::Set;
	return Mod;
}

void FGMASAbilityEndSpec::SetMana(float Target)
{
	UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
	Effect->AddToRoot();
	Kept.Add(Effect);
	FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant;
	Data.Modifiers.Add(MakeManaMod(Target - AbilityComp->GetAttributeValueByTag(ManaTag)));
	AbilityComp->ApplyAbilityEffect(Effect, Data);
	AbilityComp->ProcessAttributes(true);
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
			AbilityComp->AdvanceConfirmClockForTest(2.5);   // the confirm timeout runs on the confirm clock
			AbilityComp->TickActiveAbilitiesForTest(0.5f);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("no end event", Ability->EndAbilityEventCount, 0);
			TestEqual("cancel event once", Ability->CancelAbilityEventCount, 1);
			TestEqual("OnAbilityCancelled once", Recorder->AbilityCancelledCount, 1);
			TestFalse("no window", AbilityComp->HasActiveTag(WindowTag));
		});

		It("the server's Rejected answer cancels", [this]()
		{
			AbilityComp->bForceAuthorityForTest = false;
			constexpr int OpID = -9;
			const int AbilityID = UGMC_AbilitySystemComponent::DeriveAbilityIDFromOperationForTest(OpID, 0);
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass(), nullptr, FGameplayTag::EmptyTag,
				false, AbilityID, OpID, /*SourceCandidateIndex=*/0);
			UGMAS_TestAbility* Ability = FirstLiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			FGMASAbilitySyncMessage Answer;
			Answer.Type = EGMASAbilitySyncType::Answer;
			Answer.OperationID = OpID;
			Answer.Answer = EGMASAbilityAnswer::Rejected;
			AbilityComp->ReceiveAbilitySyncForTest(Answer);
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

	// The cost class is the UGMAS_TestCostEffect CDO, shaped per case; the test ability commits it from
	// BeginAbilityEvent when bCommitCostOnBegin is set. Mana starts at 50.
	Describe("Cost", [this]()
	{
		It("an unaffordable cost refuses activation before BeginAbility and leaves the attribute alone", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-60.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("no BeginAbilityEvent", Ability->BeginAbilityEventCount, 0);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			AbilityComp->ProcessAttributes(true);
			TestEqual("mana untouched", AbilityComp->GetAttributeValueByTag(ManaTag), 50.f);
		});

		It("an affordable cost is committed once", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-20.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->ProcessAttributes(true);
			TestEqual("mana 30", AbilityComp->GetAttributeValueByTag(ManaTag), 30.f);
		});

		// GetActiveEffectsByTag keeps a completed effect until the next cleanup pass, so the cost is
		// still found after the cancel and its bCompleted tells the story.
		It("a Ticking cost is declared and ends on cancel", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Ticking; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-10.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("cost alive", AbilityComp->GetActiveEffectsByTag(CostTag).Num(), 1);
			AbilityComp->CancelAbilitiesByTag(SwingTag);
			const TArray<UGMCAbilityEffect*> Costs = AbilityComp->GetActiveEffectsByTag(CostTag);
			if (!TestEqual("cost still registered", Costs.Num(), 1)) { return; }
			TestTrue("cost ended with the ability", Costs[0]->bCompleted);
		});

		It("GetAbilityCostValues sums per attribute and resolves attribute-sourced values", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag;
			Cost.Modifiers.Add(MakeManaMod(-10.f));
			FGMCAttributeModifier Pct; Pct.AttributeTag = ManaTag; Pct.Op = EModifierType::AddPercentageAttribute; Pct.ValueType = EGMCAttributeModifierType::AMT_Value; Pct.ModifierValue = -20.f; Pct.ValueAsAttribute = ManaTag;   // -20% of 50 = -10
			Cost.Modifiers.Add(Pct);
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			const TMap<FGameplayTag, float> Values = Ability->GetAbilityCostValues();
			TestEqual("one entry", Values.Num(), 1);
			TestEqual("-10 + -10", Values.FindRef(ManaTag), -20.f);
		});

		It("two modifiers on one attribute are judged together", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag;
			Cost.Modifiers.Add(MakeManaMod(-30.f)); Cost.Modifiers.Add(MakeManaMod(-30.f));   // each alone is payable from 50; together they are not
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("no BeginAbilityEvent", Ability->BeginAbilityEventCount, 0);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			AbilityComp->ProcessAttributes(true);
			TestEqual("mana untouched", AbilityComp->GetAttributeValueByTag(ManaTag), 50.f);
		});

		// A cost removed early is purged from ActiveEffects by the next effect tick (the real cleanup
		// pass in TickActiveEffects); at ability end its declared id would no longer resolve and the
		// [EffectLeak] tag fallback would remove the first live effect with the tag: the sibling's cost.
		// RemoveAbilityCost forgets the declaration instead.
		It("RemoveAbilityCost on a declared cost does not touch a sibling's cost at ability end", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Ticking; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-10.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* First = FirstLiveAbility();
			if (!TestNotNull("first instance", First)) { return; }
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TArray<UGMCAbilityEffect*> Costs = AbilityComp->GetActiveEffectsByTag(CostTag);
			if (!TestEqual("two live costs", Costs.Num(), 2)) { return; }

			First->RemoveAbilityCost();
			AbilityComp->TickActiveEffects(0.f);   // purges the completed cost; a Duration-0 Ticking cost never ends on its own
			Costs = AbilityComp->GetActiveEffectsByTag(CostTag);
			if (!TestEqual("only the sibling's cost is still registered", Costs.Num(), 1)) { return; }
			UGMCAbilityEffect* SiblingCost = Costs[0];
			TestFalse("sibling's cost live before the end", SiblingCost->bCompleted);

			First->CancelAbility();
			TestEqual("first Ended", First->AbilityState, EAbilityState::Ended);
			TestFalse("sibling's cost untouched by the first instance's end", SiblingCost->bCompleted);
		});

		// Networked, a Ticking removal is deferred by the grace (both sides end it at the same
		// ActionTimer), so the removed cost is still registered and live when the first instance ends
		// inside the window. The forgotten declaration must not end anything at that end, and the
		// deferred end must still land after the grace. Positive clock: EndAtActionTimer < 0 means unarmed.
		It("networked: RemoveAbilityCost inside the grace window leaves the sibling's cost live and the removed cost ends after the grace", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Ticking; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-10.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* First = FirstLiveAbility();
			if (!TestNotNull("first instance", First)) { return; }
			TArray<UGMCAbilityEffect*> Costs = AbilityComp->GetActiveEffectsByTag(CostTag);
			if (!TestEqual("first cost", Costs.Num(), 1)) { return; }
			UGMCAbilityEffect* RemovedCost = Costs[0];

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			Costs = AbilityComp->GetActiveEffectsByTag(CostTag);
			if (!TestEqual("two live costs", Costs.Num(), 2)) { return; }
			UGMCAbilityEffect* SiblingCost = Costs[0] == RemovedCost ? Costs[1] : Costs[0];

			First->RemoveAbilityCost();
			TestFalse("removed cost deferred, not ended", RemovedCost->bCompleted);
			TestEqual("removed cost armed at 1.5", RemovedCost->EndAtActionTimer, 1.5);

			AbilityComp->SetActionTimerForTest(1.25); AbilityComp->TickActiveEffects(0.25f);
			First->CancelAbility();
			TestEqual("first Ended", First->AbilityState, EAbilityState::Ended);
			TestFalse("sibling's cost live after the first end", SiblingCost->bCompleted);
			TestEqual("sibling's cost not armed", SiblingCost->EndAtActionTimer, -1.0);
			TestFalse("removed cost still inside its grace", RemovedCost->bCompleted);

			AbilityComp->SetActionTimerForTest(1.5); AbilityComp->TickActiveEffects(0.25f);
			TestTrue("removed cost ended after the grace", RemovedCost->bCompleted);
			TestFalse("sibling's cost still live", SiblingCost->bCompleted);
		});

		// The activation gate calls CanAffordAbilityCost with its default DeltaTime of one second, so a
		// Ticking cost must be affordable for a full second at activation.
		It("a Ticking -20/s cost on Mana 15 is refused at activation", [this]()
		{
			SetMana(15.f);
			TestEqual("mana 15", AbilityComp->GetAttributeValueByTag(ManaTag), 15.f);
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Ticking; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-20.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("no BeginAbilityEvent", Ability->BeginAbilityEventCount, 0);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("no cost applied", AbilityComp->GetActiveEffectsByTag(CostTag).Num(), 0);
		});

		It("a Ticking -20/s cost on Mana 25 begins", [this]()
		{
			SetMana(25.f);
			TestEqual("mana 25", AbilityComp->GetAttributeValueByTag(ManaTag), 25.f);
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Ticking; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-20.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("BeginAbilityEvent once", Ability->BeginAbilityEventCount, 1);
			TestTrue("not Ended", Ability->AbilityState != EAbilityState::Ended);
			TestEqual("cost applied", AbilityComp->GetActiveEffectsByTag(CostTag).Num(), 1);
		});

		// A Set is absolute: the gate judges the target, not a delta.
		It("a Set cost to -5 is refused", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaSetMod(-5.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			GetMutableDefault<UGMAS_TestAbility>()->bCommitCostOnBegin = true;
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("no BeginAbilityEvent", Ability->BeginAbilityEventCount, 0);
			TestEqual("Ended", Ability->AbilityState, EAbilityState::Ended);
			AbilityComp->ProcessAttributes(true);
			TestEqual("mana untouched", AbilityComp->GetAttributeValueByTag(ManaTag), 50.f);
		});

		It("a Set cost to 10 begins and GetAbilityCostValues reports 10 - 50", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaSetMod(10.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());   // not committed: the values read against Mana 50
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("BeginAbilityEvent once", Ability->BeginAbilityEventCount, 1);
			const TMap<FGameplayTag, float> Values = Ability->GetAbilityCostValues();
			TestEqual("one entry", Values.Num(), 1);
			TestEqual("10 - 50", Values.FindRef(ManaTag), -40.f);
		});

		// Hotbar / tooltip UI reads the class default object, which has no owner: the raw values.
		It("GetAbilityCostValues on the class default object returns the raw modifier values", [this]()
		{
			FGMCAbilityEffectData Cost; Cost.EffectType = EGMASEffectType::Instant; Cost.EffectTag = CostTag; Cost.Modifiers.Add(MakeManaMod(-10.f));
			GetMutableDefault<UGMAS_TestCostEffect>()->EffectData = Cost;
			GetMutableDefault<UGMAS_TestAbility>()->AbilityCost = UGMAS_TestCostEffect::StaticClass();
			const TMap<FGameplayTag, float> Values = GetDefault<UGMAS_TestAbility>()->GetAbilityCostValues();
			TestEqual("one entry", Values.Num(), 1);
			TestEqual("raw -10", Values.FindRef(ManaTag), -10.f);
		});
	});

	// TryActivateAbility copies every reflected property from the CDO (a native class built from its own
	// CDO gets only the config properties from the engine); the hand-kept list it replaced never had
	// these two.
	Describe("Instance defaults", [this]()
	{
		It("an instance carries every CDO property, including the ones the old copy block omitted", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->bApplyCooldownAtAbilityBegin = false;
			GetMutableDefault<UGMAS_TestAbility>()->ApplyEffectOnEnd.Add(UGMCAbilityEffect::StaticClass());
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(AnyAbility());
			if (!TestNotNull("instance", Ability)) { return; }
			TestFalse("bApplyCooldownAtAbilityBegin copied", Ability->bApplyCooldownAtAbilityBegin);
			TestEqual("ApplyEffectOnEnd copied", Ability->ApplyEffectOnEnd.Num(), 1);
		});

		// The once-latch is keyed on the owner class (full class path; empty for the ownerless harness component) and
		// lives for the process; it is reset first so a second in-process run sees the Error again.
		It("SetCooldownForAbility with an empty tag logs once", [this]()
		{
			UGMC_AbilitySystemComponent::ResetCooldownTagReportForTest();
			AddExpectedErrorPlain(TEXT("empty AbilityTag"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->SetCooldownForAbility(FGameplayTag(), 5.f);
			AbilityComp->SetCooldownForAbility(FGameplayTag(), 5.f);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
