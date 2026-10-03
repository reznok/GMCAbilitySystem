// Effect lifecycle specs: the grace deferral on removal, apply order and events, refusal,
// Delay, queries and removals. Headless UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent
// harness, no world, no network stack; the networked branch of RemoveActiveAbilityEffect is
// reached through the bForceNetworkedForTest seam.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestEventRecorder.h"
#include "UGMAS_TestCountingEffect.h"
#include "GMAS_TestHelpers.h"

#if WITH_DEV_AUTOMATION_TESTS

BEGIN_DEFINE_SPEC(FGMASEffectLifecycleSpec,
	"GMAS.Unit.EffectLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;
	UGMAS_TestEventRecorder*        Recorder    = nullptr;

	// Effects a case roots for its lifetime; released by TeardownHarness.
	TArray<UGMCAbilityEffect*> Kept;

	FGameplayTag HealthTag;
	FGameplayTag DrainTag;
	FGameplayTag EffectParentTag;   // GMAS.Lifecycle.Effect, parent of DrainTag
	FGameplayTag BuffTag;
	FGameplayTag BuffChildTag;
	FGameplayTag DefinitionTag;
	FGameplayTag InputTag;
	FGameplayTag ProbeEventTag;   // GMAS.Lifecycle.Event.Probe

	// The project default the cases overwrite; restored by TeardownHarness.
	float SavedDefaultClientGraceTime;

	void SetupHarness();
	void TeardownHarness();

	FGMCAttributeModifier MakeHealthMod(float Amount) const;

END_DEFINE_SPEC(FGMASEffectLifecycleSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASEffectLifecycleSpec::SetupHarness()
{
	static FNativeGameplayTag SProbeEventTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Event.Probe"), TEXT("Custom event for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	ProbeEventTag = SProbeEventTag.GetTag();
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Attribute.Health"), TEXT("Health for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SEffectParentTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Effect"), TEXT("Parent effect tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SDrainTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Effect.Drain"), TEXT("Drain effect tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBuffTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Status.Buff"), TEXT("Buff effect tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBuffChildTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Status.Buff.Strong"), TEXT("Child buff effect tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SDefinitionTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Definition.Probe"), TEXT("EffectDefinition tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SInputTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Input.Probe"), TEXT("Granted ability (input) tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag       = SHealthTag.GetTag();
	DrainTag        = SDrainTag.GetTag();
	EffectParentTag = SEffectParentTag.GetTag();
	BuffTag         = SBuffTag.GetTag();
	BuffChildTag    = SBuffChildTag.GetTag();
	DefinitionTag   = SDefinitionTag.GetTag();
	InputTag        = SInputTag.GetTag();

	SavedDefaultClientGraceTime = GetDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime;

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData Health;
	Health.AttributeTag = HealthTag;
	Health.DefaultValue = 100.f;
	Health.Clamp.Min    = 0.f;
	Health.Clamp.Max    = 200.f;
	Health.bGMCBound    = true;
	AttrData->AttributeData.Add(Health);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(GMASTest::StableActionTimer);
	AbilityComp->SilenceEffectIDWrapReportForTest();   // negative clock: ids wrap into their ranges

	Recorder = NewObject<UGMAS_TestEventRecorder>(GetTransientPackage());
	Recorder->AddToRoot();
	Recorder->Bind(AbilityComp);
}

void FGMASEffectLifecycleSpec::TeardownHarness()
{
	GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = SavedDefaultClientGraceTime;

	for (UGMCAbilityEffect* Effect : Kept) { Effect->RemoveFromRoot(); }
	Kept.Reset();

	Recorder->RemoveFromRoot();
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	Recorder = nullptr; AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASEffectLifecycleSpec::MakeHealthMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASEffectLifecycleSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// RemoveActiveAbilityEffect on a networked Ticking/Periodic effect arms EndAtActionTimer =
	// ActionTimer + grace instead of ending the effect, so client and server fire the same number
	// of applications before EndEffect. The grace is the per-effect ClientGraceTime when > 0, else
	// the project default; 0 means no deferral.
	Describe("Grace deferral on removal", [this]()
	{
		It("networked Ticking removal arms EndAtActionTimer = ActionTimer + grace and ends when the clock reaches it", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("not ended yet", Applied->bCompleted);
			TestEqual("armed at 1.5", Applied->EndAtActionTimer, 1.5);
			AbilityComp->SetActionTimerForTest(1.25); AbilityComp->TickActiveEffects(0.25f);
			TestFalse("still alive at 1.25", Applied->bCompleted);
			AbilityComp->SetActionTimerForTest(1.5); AbilityComp->TickActiveEffects(0.25f);
			TestTrue("ended at 1.5", Applied->bCompleted);
			Effect->RemoveFromRoot();
		});

		It("a Periodic removal arms the same way", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Periodic; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("not ended yet", Applied->bCompleted);
			TestEqual("armed at 1.5", Applied->EndAtActionTimer, 1.5);
			Effect->RemoveFromRoot();
		});

		It("a Persistent removal is never deferred", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});

		It("a project grace of 0 ends the effect at once", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});

		It("a per-effect ClientGraceTime defers even when the project default is 0", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.ClientGraceTime = 0.25f; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("deferred", Applied->bCompleted);
			TestEqual("armed at 1.25", Applied->EndAtActionTimer, 1.25);
			Effect->RemoveFromRoot();
		});

		It("standalone never defers", [this]()
		{
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});
	});

	// The inner ApplyAbilityEffect assigns the id and registers the instance before
	// InitializeEffect, so the events StartEffect broadcasts carry the final id and can find the
	// effect; an Instant effect starts then ends; a refused apply registers nothing.
	Describe("Apply order", [this]()
	{
		It("OnEffectApplied sees the final id and can find the instance; Instant ends as Ended with Start before End", [this]()
		{
			UGMAS_TestCountingEffect* Effect = NewObject<UGMAS_TestCountingEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { return; }
			TestEqual("one applied event", Recorder->AppliedIDs.Num(), 1);
			TestNotEqual("id was final in the event", Recorder->AppliedIDs[0], 0);
			TestEqual("id matches", Recorder->AppliedIDs[0], Applied->EffectData.EffectID);
			TestTrue("queryable from inside the event", Recorder->AppliedQueryable[0]);
			TestEqual("state Ended", Applied->CurrentState, EGMASEffectState::Ended);
			TestEqual("events: Start then End", Effect->Events, TArray<FString>({TEXT("Start"), TEXT("End")}));
			TestFalse("completed Instant not in the bound id list", AbilityComp->BoundActiveEffectIDs_Contains(Applied->EffectData.EffectID));
			TestFalse("completed Instant not Pending", AbilityComp->GetProcessedEffectIDsForTest().Contains(Applied->EffectData.EffectID));
		});

		It("an apply refused by ApplicationMustHaveTags returns nullptr, registers nothing and fires no events", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag;
			Data.ApplicationMustHaveTags.AddTag(BuffTag);   // owner does not have it
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			TestNull("refused", Applied);
			TestEqual("not registered", AbilityComp->GetActiveEffects().Num(), 0);
			TestEqual("no applied event", Recorder->AppliedIDs.Num(), 0);
			TestEqual("no removed event", Recorder->RemovedIDs.Num(), 0);
		});

		It("a buffered PredictedQueued apply returns the id the effect will carry", [this]()
		{
			int Handle = -1, Id = -1; UGMCAbilityEffect* Out = nullptr;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			const bool bOk = AbilityComp->ApplyAbilityEffect(UGMCAbilityEffect::StaticClass(), Data, EGMCAbilityEffectQueueType::PredictedQueued, Handle, Id, Out);
			TestTrue("buffered apply reported", bOk);
			TestNotEqual("id reserved", Id, -1);
			TestEqual("not applied yet", AbilityComp->GetActiveEffects().Num(), 0);
			AbilityComp->GenPredictionTick(0.f);   // drains the buffer
			TestNotNull("effect now carries the reserved id", AbilityComp->GetEffectById(Id));
		});

		It("a listener that removes the effect inside OnEffectApplied leaves no tags, modifiers or registration behind", [this]()
		{
			Recorder->bRemoveOnApplied = true;
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.bNegateEffectAtEnd = true; Data.EffectTag = DrainTag;
			Data.GrantedTags.AddTag(BuffTag); Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			// The apply had started (OnEffectApplied fired), so the instance is returned, ended.
			if (!TestNotNull("returned instance", Applied)) { return; }
			TestTrue("completed", Applied->bCompleted);
			TestEqual("state Ended", Applied->CurrentState, EGMASEffectState::Ended);
			TestEqual("applied event fired once", Recorder->AppliedIDs.Num(), 1);
			TestEqual("removed event fired once", Recorder->RemovedIDs.Num(), 1);
			TestFalse("granted tag rolled back", AbilityComp->HasActiveTag(BuffTag));
			AbilityComp->ProcessAttributes(true);
			TestEqual("health untouched", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			AbilityComp->TickActiveEffects(0.f);
			TestFalse("id gone after the cleanup pass", AbilityComp->GetActiveEffects().Contains(Effect->EffectData.EffectID));
		});

		It("an effect that removes itself from its own StartEffectEvent ends with its modifier rolled back and no duplicate events", [this]()
		{
			UGMAS_TestCountingEffect* Effect = NewObject<UGMAS_TestCountingEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			Effect->bRemoveSelfOnStartEvent = true;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.bNegateEffectAtEnd = true; Data.EffectTag = DrainTag;
			Data.GrantedTags.AddTag(BuffTag); Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("returned instance", Applied)) { return; }
			TestTrue("completed", Applied->bCompleted);
			TestEqual("state Ended", Applied->CurrentState, EGMASEffectState::Ended);
			AbilityComp->ProcessAttributes(true);
			TestEqual("modifier applied then rolled back", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestFalse("granted tag rolled back", AbilityComp->HasActiveTag(BuffTag));
			TestEqual("events: Start then End, once each", Effect->Events, TArray<FString>({TEXT("Start"), TEXT("End")}));
		});
	});

	// Before StartTime an effect only waits for its start: no TickEvent, no CurrentDuration, no
	// modifiers.
	Describe("Delay", [this]()
	{
		It("no TickEvent and no duration before StartTime", [this]()
		{
			AbilityComp->SetActionTimerForTest(1.0);
			UGMAS_TestCountingEffect* Effect = NewObject<UGMAS_TestCountingEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Delay = 1.0; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { return; }
			AbilityComp->SetActionTimerForTest(1.5); AbilityComp->TickActiveEffects(0.5f); AbilityComp->ProcessAttributes(true);
			TestEqual("no Tick before start", Effect->Count(TEXT("Tick")), 0);
			TestEqual("duration 0 before start", Applied->GetCurrentDuration(), 0.f);
			TestEqual("health untouched", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			AbilityComp->SetActionTimerForTest(2.0); AbilityComp->TickActiveEffects(0.5f);
			TestEqual("started at StartTime", Applied->CurrentState, EGMASEffectState::Started);
			AbilityComp->SetActionTimerForTest(2.5); AbilityComp->TickActiveEffects(0.5f);
			TestEqual("ticks after start", Effect->Count(TEXT("Tick")), 1);
		});

		It("a MustMaintainQuery unsatisfied at StartTime refuses the delayed start", [this]()
		{
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Delay = 1.0; Data.Modifiers.Add(MakeHealthMod(-10.f));
			Data.MustMaintainQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(BuffTag));   // owner does not have it
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("registered while waiting", Applied)) { return; }
			AbilityComp->SetActionTimerForTest(2.0); AbilityComp->TickActiveEffects(1.f); AbilityComp->ProcessAttributes(true);
			TestEqual("never applied", Recorder->AppliedIDs.Num(), 0);
			TestTrue("ended at StartTime", Applied->bCompleted);
			TestEqual("health untouched", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
		});
	});

	Describe("Queries and removals", [this]()
	{
		It("GetActiveEffectsByTag skips invalid entries in both match modes", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag;
			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GetActiveEffectsForTest().Add(-9999, nullptr);
			TestEqual("exact: one", AbilityComp->GetActiveEffectsByTag(DrainTag, true).Num(), 1);
			TestEqual("hierarchical: one", AbilityComp->GetActiveEffectsByTag(EffectParentTag, false).Num(), 1);
			AbilityComp->GetActiveEffectsForTest().Remove(-9999);
		});

		It("RemoveEffectsByQuery survives an end hook that applies another effect", [this]()
		{
			FGameplayTagQuery Query = FGameplayTagQuery::MakeQuery_MatchAnyTags(FGameplayTagContainer(DefinitionTag));
			for (int i = 0; i < 3; i++)
			{
				UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot(); Kept.Add(Effect);
				FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag;
				Data.EffectDefinition.AddTag(DefinitionTag);
				Data.ApplyEffectOnEnd.Add(UGMCAbilityEffect::StaticClass());   // mutates ActiveEffects during removal
				AbilityComp->ApplyAbilityEffect(Effect, Data);
			}
			const int Removed = AbilityComp->RemoveEffectsByQuery(Query, EGMCAbilityEffectQueueType::Predicted);
			TestEqual("three removed", Removed, 3);
			// 3 removed effects + 3 chain-applied Instant effects, all completed and still registered
			// until the next cleanup pass.
			TestEqual("six registered before cleanup", AbilityComp->GetActiveEffects().Num(), 6);
		});

		It("an ability granted by two effects survives the first one's end", [this]()
		{
			UGMCAbilityEffect* A = NewObject<UGMCAbilityEffect>(GetTransientPackage()); A->AddToRoot(); Kept.Add(A);
			UGMCAbilityEffect* B = NewObject<UGMCAbilityEffect>(GetTransientPackage()); B->AddToRoot(); Kept.Add(B);
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag; Data.GrantedAbilities.AddTag(InputTag);
			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(A, Data);
			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(B, Data);
			TestTrue("granted", AbilityComp->HasGrantedAbilityTag(InputTag));
			AbilityComp->RemoveActiveAbilityEffect(AppliedA);
			TestTrue("still granted while B lives", AbilityComp->HasGrantedAbilityTag(InputTag));
			AbilityComp->RemoveActiveAbilityEffect(AppliedB);
			TestFalse("revoked once both ended", AbilityComp->HasGrantedAbilityTag(InputTag));
		});

		It("ServerInstantAttribute with CancelAbilityOnEnd falls back to ServerAuth", [this]()
		{
			AddExpectedErrorPlain(TEXT("Falling back to ServerAuth"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = true;
			int Handle = -1, Id = -1; UGMCAbilityEffect* Out = nullptr;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			Data.CancelAbilityOnEnd.AddTag(InputTag);
			const bool bOk = AbilityComp->ApplyAbilityEffect(UGMCAbilityEffect::StaticClass(), Data, EGMCAbilityEffectQueueType::ServerInstantAttribute, Handle, Id, Out);
			TestTrue("queued on the ServerAuth path", bOk);
			TestTrue("with a server-auth id", Id >= UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset && Id < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
			TestEqual("nothing applied locally", AbilityComp->GetActiveEffects().Num(), 0);
		});

		It("ServerInstantAttribute with a non-Instant type in the inline data falls back to ServerAuth", [this]()
		{
			AddExpectedErrorPlain(TEXT("Falling back to ServerAuth"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = true;
			int Handle = -1, Id = -1; UGMCAbilityEffect* Out = nullptr;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			const bool bOk = AbilityComp->ApplyAbilityEffect(UGMCAbilityEffect::StaticClass(), Data, EGMCAbilityEffectQueueType::ServerInstantAttribute, Handle, Id, Out);
			TestTrue("queued on the ServerAuth path", bOk);
			TestTrue("with a server-auth id", Id >= UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset && Id < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
			TestEqual("nothing applied locally", AbilityComp->GetActiveEffects().Num(), 0);
		});

		It("ServerInstantAttribute without side effects applies at once", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			int Handle = -1, Id = -1; UGMCAbilityEffect* Out = nullptr;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			const bool bOk = AbilityComp->ApplyAbilityEffect(UGMCAbilityEffect::StaticClass(), Data, EGMCAbilityEffectQueueType::ServerInstantAttribute, Handle, Id, Out);
			AbilityComp->ProcessAttributes(true);
			TestTrue("applied", bOk);
			TestEqual("health 90", AbilityComp->GetAttributeValueByTag(HealthTag), 90.f);
		});

		It("exact tag removal does not remove children; the hierarchical removal does", [this]()
		{
			UGMCAbilityEffect* Parent = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Parent->AddToRoot(); Kept.Add(Parent);
			UGMCAbilityEffect* Child  = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Child->AddToRoot();  Kept.Add(Child);
			FGMCAbilityEffectData P; P.EffectType = EGMASEffectType::Persistent; P.EffectTag = BuffTag;
			FGMCAbilityEffectData C; C.EffectType = EGMASEffectType::Persistent; C.EffectTag = BuffChildTag;
			AbilityComp->ApplyAbilityEffect(Parent, P);
			AbilityComp->ApplyAbilityEffect(Child, C);
			AbilityComp->RemoveEffectByTagSafe(BuffTag, -1, EGMCAbilityEffectQueueType::Predicted);   // exact: what RemoveSynchronizedTag now uses
			TestEqual("child alive", AbilityComp->GetActiveEffectsByTag(BuffChildTag, true).Num(), 1);
			TestFalse("child not ended by the exact removal", Child->bCompleted);
			TestTrue("parent ended", AbilityComp->GetActiveEffectsByTag(BuffTag, true)[0]->bCompleted);
			AbilityComp->RemoveActiveAbilityEffectByTag(BuffTag, EGMCAbilityEffectQueueType::Predicted, true);   // hierarchical
			TestTrue("child ended by the hierarchical removal", Child->bCompleted);
		});
	});

	Describe("Custom events", [this]()
	{
		It("FireCustomEvent reaches OnCustomEvent on the standalone path", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			AbilityComp->bForceNoAckClientForTest = true;
			AbilityComp->BindServerOpForcedDelegateForTest();
			AbilityComp->FireCustomEvent(ProbeEventTag, FInstancedStruct());
			AbilityComp->GenAncillaryTick(0.f, false);     // zero grace: forced on this tick
			TestEqual("one custom event", Recorder->CustomEvents.Num(), 1);
			if (Recorder->CustomEvents.Num() == 1) { TestEqual("its tag", Recorder->CustomEvents[0], ProbeEventTag); }
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
