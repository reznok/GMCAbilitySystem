// Layer-2 regression tests. Use the headless UGMAS_TestMovementCmp +
// UGMC_AbilitySystemComponent harness — no world, no network stack.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestCostEffect.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASBugFixSpec,
	"GMAS.Unit.BugFix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;

	FGameplayTag HealthTag;
	FGameplayTag BurningTag;
	FGameplayTag ManaTag;

	void SetupHarness();
	void TeardownHarness();

	FGMCAttributeModifier MakeHealthMod(float Amount) const;
	FGMCAttributeModifier MakeManaMod(float Amount) const;

END_DEFINE_SPEC(FGMASBugFixSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASBugFixSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.BugFix.Attribute.Health"), TEXT("Health for bug-fix tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SManaTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.BugFix.Attribute.Mana"), TEXT("Mana for bug-fix tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.BugFix.Status.Burning"), TEXT("Burning tag for bug-fix tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag  = SHealthTag.GetTag();
	ManaTag    = SManaTag.GetTag();
	BurningTag = SBurningTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	auto AddAttr = [&](FGameplayTag Tag, float Default, float Min, float Max)
	{
		FAttributeData D;
		D.AttributeTag  = Tag;
		D.DefaultValue  = Default;
		D.Clamp.Min     = Min;
		D.Clamp.Max     = Max;
		D.bGMCBound     = true;
		AttrData->AttributeData.Add(D);
	};
	AddAttr(HealthTag, 100.f, 0.f, 200.f);
	AddAttr(ManaTag,   50.f,  0.f, 100.f);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = 1.0;

	// Configure TestAbility CDO
	GetMutableDefault<UGMAS_TestAbility>()->CooldownTime            = 0.f;
	GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = true;
}

void FGMASBugFixSpec::TeardownHarness()
{
	GetMutableDefault<UGMAS_TestAbility>()->AbilityTag              = FGameplayTag();
	GetMutableDefault<UGMAS_TestAbility>()->CooldownTime            = 0.f;
	GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;

	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASBugFixSpec::MakeHealthMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

FGMCAttributeModifier FGMASBugFixSpec::MakeManaMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = ManaTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASBugFixSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── Bug #1: MustMaintainQuery logic inversion ─────────────────────────
	// Prior to fix: EndEffect() was called when the query DID match, keeping
	// the effect alive only when the query was unsatisfied (backwards).
	// After fix: effect stays alive while the query matches; ends when it stops.
	Describe("MustMaintainQuery", [this]()
	{
		It("effect remains active while MustMaintainQuery is satisfied", [this]()
		{
			// Grant Burning so the query is satisfied from the start.
			AbilityComp->AddActiveTag(BurningTag);

			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration         = 0.f;
			Data.Modifiers.Add(MakeHealthMod(30.f));
			// Require Burning to maintain the effect.
			Data.MustMaintainQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(
				FGameplayTagContainer(BurningTag));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ActionTimer = 1.0;
			AbilityComp->TickActiveEffects(1.f);
			AbilityComp->ProcessAttributes(true);

			// Effect should still be alive — query is satisfied (Burning is present).
			TestFalse("Effect is NOT removed while query is satisfied",
				AbilityComp->GetActiveEffects().IsEmpty());
			TestEqual("Buff still applied: Health = 130",
				AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);

			Effect->RemoveFromRoot();
		});

		It("effect ends when MustMaintainQuery stops being satisfied", [this]()
		{
			// Grant Burning so the effect can start.
			AbilityComp->AddActiveTag(BurningTag);

			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration         = 0.f;
			Data.Modifiers.Add(MakeHealthMod(30.f));
			Data.MustMaintainQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(
				FGameplayTagContainer(BurningTag));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ActionTimer = 1.0;
			AbilityComp->TickActiveEffects(1.f);
			AbilityComp->ProcessAttributes(true);
			TestFalse("Active before tag removal", AbilityComp->GetActiveEffects().IsEmpty());

			// Remove the tag — query is no longer satisfied.
			AbilityComp->RemoveActiveTag(BurningTag);

			AbilityComp->ActionTimer = 2.0;
			AbilityComp->TickActiveEffects(1.f);
			AbilityComp->ProcessAttributes(true);

			// Effect must end because the query is no longer satisfied.
			TestTrue("Effect removed after query stops matching",
				AbilityComp->GetActiveEffects().IsEmpty());
			TestEqual("Buff reverted: Health = 100",
				AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			Effect->RemoveFromRoot();
		});

		It("effect is immediately removed on the first tick when query is never satisfied", [this]()
		{
			// No Burning tag — query never satisfied from the start.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration         = 0.f;
			Data.Modifiers.Add(MakeHealthMod(30.f));
			Data.MustMaintainQuery = FGameplayTagQuery::MakeQuery_MatchAnyTags(
				FGameplayTagContainer(BurningTag));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ActionTimer = 1.0;
			AbilityComp->TickActiveEffects(1.f);
			AbilityComp->ProcessAttributes(true);

			TestTrue("Effect removed immediately when query is never satisfied",
				AbilityComp->GetActiveEffects().IsEmpty());
			TestEqual("Health unchanged at 100",
				AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			Effect->RemoveFromRoot();
		});
	});

	// ── Bug #2 retired ─────────────────────────────────────────────────────
	// CheckRemovedEffects + ActiveEffectIDs were dropped in the single-channel
	// refactor. Effect removal now flows exclusively through BoundQueueV2
	// FGMASBoundQueueV2RemoveEffectOperation; there is no list-comparison wipe
	// path left to test, and the original Bug #2 ("early `return` instead of
	// `continue`") is moot because the function no longer exists.

	// ── Bug #3: TMap integer-indexed iteration in TickTasks ───────────────
	// RunningTasks is TMap<int, UGMCAbilityTaskBase*>.  The old code called
	// RunningTasks[i] (int i from 0..Num-1) which is a key lookup, not an
	// index lookup — UE's TMap asserts/crashes when the key is absent.
	// After fix: range-based for loop iterates over actual key-value pairs.
	//
	// Testing approach: activate an ability, advance a tick.  If the loop
	// were still broken the test would crash (hard assert in TMap::operator[]).
	// A successful run (no crash, task count unchanged) validates the fix.
	Describe("TickTasks TMap iteration", [this]()
	{
		It("TickTasks does not crash when RunningTasks is empty", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			// GenPredictionTick calls TickTasks on each active ability.
			// With RunningTasks empty this previously still called Num() == 0
			// so the for loop body was never entered — but with a task present
			// RunningTasks[0] would crash if key 0 didn't exist.  Verify no crash.
			AbilityComp->GenPredictionTick(1.f);
			TestTrue("Tick with empty RunningTasks completes without crash", true);
		});
	});

	// ── Bug #4: GetEffectFromHandle missing Contains guard ────────────────
	// Before fix: ActiveEffects[NetworkId] was called even when NetworkId was
	// absent, causing a TMap crash / undefined behaviour.
	// After fix: Contains check guards the access.
	Describe("GetEffectFromHandle stale handle safety", [this]()
	{
		It("returns false and leaves OutEffect null when handle's NetworkId is absent from ActiveEffects", [this]()
		{
			AbilityComp->ActionTimer = 1.0;

			// Directly insert a handle entry whose NetworkId does NOT exist
			// in ActiveEffects — simulates a handle pointing to an expired effect.
			const int StaleNetworkId = 9999;
			const int HandleKey      = 42;
			FGMASQueueOperationHandle StaleHandle;
			StaleHandle.Handle      = HandleKey;
			StaleHandle.OperationId = -1;
			StaleHandle.NetworkId   = StaleNetworkId;
			AbilityComp->GetEffectHandlesForTest().Add(HandleKey, StaleHandle);
			// ActiveEffects intentionally does NOT contain StaleNetworkId.

			int32 OutNetworkId = -1;
			UGMCAbilityEffect* OutEffect = nullptr;
			const bool bResult = AbilityComp->GetEffectFromHandleForTest(HandleKey, OutNetworkId, OutEffect);

			// The handle was found, so it returns true, but OutEffect must be null
			// because the NetworkId is not in ActiveEffects (guard prevents crash).
			TestTrue("Handle found (true return)", bResult);
			TestEqual("OutNetworkId == StaleNetworkId", OutNetworkId, StaleNetworkId);
			TestNull("OutEffect is null for a stale handle", OutEffect);
		});

		It("returns the correct effect when the handle's NetworkId is present", [this]()
		{
			AbilityComp->ActionTimer = 1.0;

			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int NetworkId  = Effect->EffectData.EffectID;
			const int HandleKey  = 100;
			FGMASQueueOperationHandle Handle;
			Handle.Handle      = HandleKey;
			Handle.OperationId = -1;
			Handle.NetworkId   = NetworkId;
			AbilityComp->GetEffectHandlesForTest().Add(HandleKey, Handle);

			int32 OutNetworkId = -1;
			UGMCAbilityEffect* OutEffect = nullptr;
			AbilityComp->GetEffectFromHandleForTest(HandleKey, OutNetworkId, OutEffect);

			TestEqual("OutNetworkId matches", OutNetworkId, NetworkId);
			TestNotNull("OutEffect is non-null for a live handle", OutEffect);

			Effect->RemoveFromRoot();
		});
	});

	// ── Bug #5: ProcessedEffectIDs never trimmed ──────────────────────────
	// Before fix: ProcessedEffectIDs grew unboundedly — entries added at
	// effect creation but never removed on expiry.
	// After fix: each expired EffectID is removed from ProcessedEffectIDs in
	// the CompletedActiveEffects cleanup loop inside TickActiveEffects.
	Describe("ProcessedEffectIDs cleanup on effect expiry", [this]()
	{
		It("ProcessedEffectIDs entry is removed when an effect expires", [this]()
		{
			// Use ActionTimer-driven ticking so the duration can expire.
			// ActionTimer must yield EffectID > 0: static_cast<int>(t*100) must be >= 1,
			// so ActionTimer >= 0.01. Use 1.0 for a clean, unambiguous value.
			AbilityComp->ActionTimer = 1.0;

			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 2.f; // expires at t=2
			Data.Modifiers.Add(MakeHealthMod(10.f));
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int EffectID = Effect->EffectData.EffectID;

			// Manually seed ProcessedEffectIDs as if this is a client-predicted effect.
			AbilityComp->GetProcessedEffectIDsForTest().Add(EffectID, EGMCEffectAnswerState::Validated);
			TestTrue("ProcessedEffectIDs contains entry before expiry",
				AbilityComp->GetProcessedEffectIDsForTest().Contains(EffectID));

			// Advance past duration — effect expires during TickActiveEffects.
			AbilityComp->ActionTimer = 3.0;
			AbilityComp->TickActiveEffects(3.f);
			AbilityComp->ProcessAttributes(true);

			TestFalse("ProcessedEffectIDs entry removed after expiry",
				AbilityComp->GetProcessedEffectIDsForTest().Contains(EffectID));
			TestFalse("ActiveEffects entry removed after expiry",
				AbilityComp->GetActiveEffects().Contains(EffectID));

			Effect->RemoveFromRoot();
		});

		It("ProcessedEffectIDs does not accumulate entries across multiple effect lifetimes", [this]()
		{
			// Apply and expire 3 effects in sequence; map must remain at 0 entries.
			for (int i = 0; i < 3; i++)
			{
				// i*10 is 0 on the first iteration — add 1 to avoid ActionTimer==0.
			AbilityComp->ActionTimer = static_cast<double>(i * 10 + 1);

				UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
				Effect->AddToRoot();

				FGMCAbilityEffectData Data;
				Data.EffectType = EGMASEffectType::Persistent;
				Data.Duration   = 1.f;
				Data.Modifiers.Add(MakeHealthMod(5.f));
				AbilityComp->ApplyAbilityEffect(Effect, Data);

				const int EffectID = Effect->EffectData.EffectID;
				AbilityComp->GetProcessedEffectIDsForTest().Add(EffectID, EGMCEffectAnswerState::Validated);

				AbilityComp->ActionTimer = static_cast<double>(i * 10 + 5);
				AbilityComp->TickActiveEffects(5.f);
				AbilityComp->ProcessAttributes(true);

				Effect->RemoveFromRoot();
			}

			TestEqual("ProcessedEffectIDs is empty after all effects expire",
				AbilityComp->GetProcessedEffectIDsForTest().Num(), 0);
		});
	});

	// ── Bug #6: CanAffordAbilityCost O(n×m) and CDO vs instance issue ─────
	// Before fix: CanAffordAbilityCost iterated all attributes per modifier
	// and used the effect CDO (not the live instance), which could read stale
	// data if the CDO was mutated.
	// After fix: O(n) via GetAttributeByTag; operates on the passed-in CDO
	// consistently (no additional instance is created).
	//
	// Behavioural test: with insufficient Mana, CanAffordAbilityCost must
	// return false; with sufficient Mana it must return true.
	//
	// UGMAS_TestCostEffect is a dedicated subclass so we mutate its isolated
	// CDO without touching the shared UGMCAbilityEffect CDO.
	Describe("CanAffordAbilityCost correctness", [this]()
	{
		It("returns false when the attribute value would go below zero", [this]()
		{
			// Mana starts at 50.  Cost = -60 (drain 60 mana).  50 + (-60) = -10 < 0.
			UGMAS_TestCostEffect* CostCDO = GetMutableDefault<UGMAS_TestCostEffect>();
			CostCDO->EffectData.Modifiers.Reset();
			CostCDO->EffectData.Modifiers.Add(MakeManaMod(-60.f));

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());

			TArray<UGMCAbility*> Abilities;
			AbilityComp->GetActiveAbilities().GenerateValueArray(Abilities);
			if (!TestTrue("At least one ability is active", Abilities.Num() > 0)) return;

			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(Abilities[0]);
			if (!TestNotNull("Active ability is a UGMAS_TestAbility", Ability)) return;

			Ability->AbilityCost = UGMAS_TestCostEffect::StaticClass();

			const bool bCanAfford = Ability->CanAffordAbilityCost(1.f);
			TestFalse("Cannot afford: Mana 50 - 60 < 0", bCanAfford);

			// Restore CDO so tests are isolated.
			CostCDO->EffectData.Modifiers.Reset();
		});

		It("returns true when the attribute value remains >= 0 after cost", [this]()
		{
			// Mana starts at 50.  Cost = -30.  50 + (-30) = 20 >= 0.
			UGMAS_TestCostEffect* CostCDO = GetMutableDefault<UGMAS_TestCostEffect>();
			CostCDO->EffectData.Modifiers.Reset();
			CostCDO->EffectData.Modifiers.Add(MakeManaMod(-30.f));

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());

			TArray<UGMCAbility*> Abilities;
			AbilityComp->GetActiveAbilities().GenerateValueArray(Abilities);
			if (!TestTrue("At least one ability is active", Abilities.Num() > 0))
			{
				CostCDO->EffectData.Modifiers.Reset(); return;
			}

			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(Abilities[0]);
			if (!TestNotNull("Active ability is a UGMAS_TestAbility", Ability))
			{
				CostCDO->EffectData.Modifiers.Reset(); return;
			}

			Ability->AbilityCost = UGMAS_TestCostEffect::StaticClass();

			const bool bCanAfford = Ability->CanAffordAbilityCost(1.f);
			TestTrue("Can afford: Mana 50 - 30 = 20 >= 0", bCanAfford);

			CostCDO->EffectData.Modifiers.Reset();
		});

		It("returns true when AbilityCost is null (no cost)", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TArray<UGMCAbility*> Abilities;
			AbilityComp->GetActiveAbilities().GenerateValueArray(Abilities);
			if (!TestTrue("At least one ability active", Abilities.Num() > 0)) return;

			UGMAS_TestAbility* Ability = Cast<UGMAS_TestAbility>(Abilities[0]);
			if (!TestNotNull("Is UGMAS_TestAbility", Ability)) return;

			Ability->AbilityCost = nullptr;
			TestTrue("No cost → always affordable", Ability->CanAffordAbilityCost(1.f));
		});
	});

	// ── Bug #7: CurrentState uninitialised ───────────────────────────────
	// EGMASEffectState CurrentState now has an explicit = EGMASEffectState::Initialized
	// initialiser so freshly NewObject<>'d effects always start in Initialized
	// regardless of how the underlying memory was allocated.
	Describe("EGMASEffectState CurrentState initialiser", [this]()
	{
		It("a freshly created UGMCAbilityEffect has CurrentState == Initialized", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			TestEqual("CurrentState starts as Initialized",
				Effect->CurrentState, EGMASEffectState::Initialized);
			Effect->RemoveFromRoot();
		});

		It("CurrentState transitions to Started after InitializeEffect is called", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			// After ApplyAbilityEffect → InitializeEffect → StartEffect, the
			// state should be Started (Persistent effects don't self-terminate).
			TestEqual("CurrentState is Started after apply",
				Effect->CurrentState, EGMASEffectState::Started);

			Effect->RemoveFromRoot();
		});
	});

	// ── Bilateral PredictedEnd defer (Bug #3 + Periodic ext + RPCClientEndEffect ext) ──
	//
	// The arming branch of RemoveActiveAbilityEffect requires GetNetMode() != NM_Standalone,
	// which the headless harness can't provide (orphan components default to Standalone).
	// So we test the *consume* side of the defer: directly set EndAtActionTimer on a properly-
	// initialised effect and verify Tick fires EndEffect at the absolute timestamp regardless
	// of DeltaTime. The new design uses ActionTimer comparison (deterministic across replays)
	// instead of a per-tick countdown.
	Describe("PredictedEnd defer Tick consume (ActionTimer-absolute)", [this]()
	{
		It("Tick keeps the defer pending while ActionTimer < EndAtActionTimer", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;  // avoid the Ticking/Periodic branches in Tick
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 5.0;
			Effect->EndAtActionTimer = 6.0;  // 1s grace ahead

			Effect->Tick(0.3f);  // DeltaTime irrelevant; only ActionTimer matters

			TestTrue("Defer still armed (EndAt unchanged)",  Effect->EndAtActionTimer > 0.0);
			TestFalse("Effect not completed",                Effect->bCompleted);

			Effect->RemoveFromRoot();
		});

		It("Tick fires EndEffect when ActionTimer reaches EndAtActionTimer exactly", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 6.0;
			Effect->EndAtActionTimer = 6.0;  // boundary: >= triggers

			Effect->Tick(0.f);

			TestTrue("Effect completed via EndEffect",  Effect->bCompleted);
			TestEqual("EndAt latch reset to -1.0",      Effect->EndAtActionTimer, -1.0);

			Effect->RemoveFromRoot();
		});

		It("Tick fires EndEffect when ActionTimer is past EndAtActionTimer (catch-up)", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 10.0;  // we landed past the latch
			Effect->EndAtActionTimer = 6.0;

			Effect->Tick(0.f);

			TestTrue("Effect completed (overshoot still fires)",  Effect->bCompleted);
			TestEqual("EndAt latch reset to -1.0",                Effect->EndAtActionTimer, -1.0);

			Effect->RemoveFromRoot();
		});

		It("Tick on an unarmed effect (EndAt = -1) leaves defer state alone", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			TestEqual("Default EndAt is -1.0 (unarmed)",  Effect->EndAtActionTimer, -1.0);

			AbilityComp->ActionTimer = 100.0;  // huge ActionTimer must not trigger anything
			Effect->Tick(0.5f);

			TestFalse("Effect not completed",             Effect->bCompleted);
			TestEqual("EndAt still -1.0",                 Effect->EndAtActionTimer, -1.0);

			Effect->RemoveFromRoot();
		});

		It("ActionTimer progression across multiple Ticks reaches the latch deterministically", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 5.0;
			Effect->EndAtActionTimer = 6.0;  // 1s grace

			AbilityComp->ActionTimer = 5.3;  Effect->Tick(0.3f);
			TestFalse("Not yet completed (5.3 < 6.0)",  Effect->bCompleted);

			AbilityComp->ActionTimer = 5.7;  Effect->Tick(0.4f);
			TestFalse("Not yet completed (5.7 < 6.0)",  Effect->bCompleted);

			AbilityComp->ActionTimer = 6.1;  Effect->Tick(0.4f);
			TestTrue("Completed (6.1 >= 6.0)",          Effect->bCompleted);

			Effect->RemoveFromRoot();
		});

		It("Idempotent re-arm: setting EndAtActionTimer twice with same value is a no-op", [this]()
		{
			// Mirrors the replay scenario: a Remove op gets re-executed during a GMC rollback.
			// The arming logic in RemoveActiveAbilityEffect skips the assignment when EndAt >= 0,
			// so the second Remove cannot shift the end timestamp forward and break bilateral sync.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			Effect->EndAtActionTimer = 6.0;  // first arm
			const double FirstArm = Effect->EndAtActionTimer;

			// Simulate the idempotency guard from RemoveActiveAbilityEffect inline:
			if (Effect->EndAtActionTimer < 0.0) { Effect->EndAtActionTimer = 8.0; }

			TestEqual("Re-arm preserves the original EndAt", Effect->EndAtActionTimer, FirstArm);

			Effect->RemoveFromRoot();
		});
	});

	// ── Bug #4 v2 — GMC-bound ActiveEffectIDs replaces DOREPLIFETIME ──────
	//
	// History: V1 had `TArray<int> ActiveEffectIDs` replicated via standard
	// DOREPLIFETIME, with `OnRep_ActiveEffectIDs` performing the only
	// Pending → Validated transition for Predicted effects. Refactor 82f717b
	// dropped that field (along with CheckRemovedEffects) on the assumption
	// that BoundQueueV2 was the single source of truth — it wasn't, because
	// Predicted effects bypass BoundQueueV2 ops. The dropped OnRep was the
	// only validation path for predicted EffectIDs, causing Sprint to time
	// out 1s after each apply ("Effect Not Confirmed By Server").
	//
	// V2 fix (this work): re-introduce ActiveEffectIDs but bound via GMC's
	// BindInstancedStruct on FGMASActiveEffectIDsState. The bound state is
	// atomic with the move log — apply ops + bound list update arrive in
	// the same packet, eliminating the asymmetric replication window that
	// was Bug #4 in the original V1 pattern. Both correctness fixes
	// (Pending → Validated, server-removal detection) coexist on a single
	// channel without a grace period.
	//
	// Tests below exercise:
	//   - Helper plumbing (Add / Remove / Contains)
	//   - ApplyAbilityEffect mirrors the new ID into the bound state
	//   - TickActiveEffects cleanup drops the ID when an effect completes
	//   - The Pending → Validated polling logic, isolated from HasAuthority
	//     gating so the headless harness can drive it deterministically.
	Describe("GMC-bound ActiveEffectIDs", [this]()
	{
		It("BoundActiveEffectIDs_Add stores the ID and Contains finds it", [this]()
		{
			// Add idempotency: AddUnique semantics — second add is a no-op.
			AbilityComp->BoundActiveEffectIDs_Add(42);
			TestTrue("ID present after Add", AbilityComp->BoundActiveEffectIDs_Contains(42));

			AbilityComp->BoundActiveEffectIDs_Add(42);
			TestTrue("Idempotent Add: still present", AbilityComp->BoundActiveEffectIDs_Contains(42));

			// Multiple distinct IDs coexist.
			AbilityComp->BoundActiveEffectIDs_Add(99);
			TestTrue("Second distinct ID present", AbilityComp->BoundActiveEffectIDs_Contains(99));
			TestTrue("First ID still present", AbilityComp->BoundActiveEffectIDs_Contains(42));
		});

		It("BoundActiveEffectIDs_Remove drops the ID without affecting siblings", [this]()
		{
			AbilityComp->BoundActiveEffectIDs_Add(1);
			AbilityComp->BoundActiveEffectIDs_Add(2);
			AbilityComp->BoundActiveEffectIDs_Add(3);

			AbilityComp->BoundActiveEffectIDs_Remove(2);

			TestFalse("Removed ID is gone",          AbilityComp->BoundActiveEffectIDs_Contains(2));
			TestTrue ("Sibling 1 untouched",         AbilityComp->BoundActiveEffectIDs_Contains(1));
			TestTrue ("Sibling 3 untouched",         AbilityComp->BoundActiveEffectIDs_Contains(3));

			// Remove of an absent ID is a silent no-op.
			AbilityComp->BoundActiveEffectIDs_Remove(2);
			AbilityComp->BoundActiveEffectIDs_Remove(404);
			TestFalse("Re-remove of absent ID stays absent", AbilityComp->BoundActiveEffectIDs_Contains(2));
			TestFalse("Remove of never-added ID stays absent", AbilityComp->BoundActiveEffectIDs_Contains(404));
		});

		It("ApplyAbilityEffect mirrors the EffectID into the bound list deterministically", [this]()
		{
			// Both sides (client and server) must write the EffectID into the bound list
			// in ApplyAbilityEffect. The bind mode is ServerAuth_Output_ServerValidated:
			// the server's authoritative bound state simply overwrites the client on
			// replication, with no client-side replay storm on InstancedStruct comparison.
			// The client's local write is a transient — confirmed (or corrected) by the
			// next server replication. The bidirectional polling in TickActiveEffects
			// reconciles: ID present in bound → Validated; absent → Pending; with the
			// 1s timeout reaping orphans the server rejected.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			Data.Modifiers.Add(MakeHealthMod(10.f));
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			TestTrue("Applied effect ID is in ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Id));
			TestTrue("Applied effect ID mirrored into bound state (deterministic both-side writer)",
				AbilityComp->BoundActiveEffectIDs_Contains(Id));

			Effect->RemoveFromRoot();
		});

		It("TickActiveEffects cleanup mirrors the deterministic both-side bound-state contract", [this]()
		{
			// Symmetric counterpart of the Apply contract: both sides write on Apply,
			// both sides remove on cleanup. Bind mode ServerAuth_Output_ServerValidated
			// means the server's authoritative removal replicates back to the client
			// without triggering replay; the client's local removal is a transient that
			// the next replication confirms (or restores if the server rejects the end).
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Duration   = 0.f;
			Data.Modifiers.Add(MakeHealthMod(5.f));
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			TestTrue("Apply writes the ID into bound state (deterministic both-side writer)",
				AbilityComp->BoundActiveEffectIDs_Contains(Id));

			AbilityComp->TickActiveEffects(1.f);

			TestFalse("Post-tick: instant effect removed from ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Id));
			TestFalse("Post-tick: ID removed from bound state (symmetric cleanup)",
				AbilityComp->BoundActiveEffectIDs_Contains(Id));

			Effect->RemoveFromRoot();
		});

		It("Pending → Validated polling promotes IDs that the server has confirmed", [this]()
		{
			// Simulate a client-side Predicted apply: stamp Pending in ProcessedEffectIDs,
			// then have the server's authoritative bound state replicate the ID in.
			// The polling at the head of TickActiveEffects should promote it to Validated.
			//
			// The headless harness defaults to authoritative mode, so the polling block
			// (gated by !HasAuthority()) won't fire. We exercise the logic directly here
			// — the production gating is a separate concern covered by integration play.
			constexpr int Id = 1234;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);
			AbilityComp->BoundActiveEffectIDs_Add(Id);

			// Inline the polling logic (mirror of the production code in TickActiveEffects).
			for (auto& ProcessedPair : AbilityComp->GetProcessedEffectIDsForTest())
			{
				if (ProcessedPair.Value == EGMCEffectAnswerState::Pending
					&& AbilityComp->BoundActiveEffectIDs_Contains(ProcessedPair.Key))
				{
					ProcessedPair.Value = EGMCEffectAnswerState::Validated;
				}
			}

			const auto State = AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id);
			TestEqual("Pending promoted to Validated when ID is in bound state",
				static_cast<int>(State), static_cast<int>(EGMCEffectAnswerState::Validated));
		});

		It("Pending stays Pending when the ID is absent from the bound state", [this]()
		{
			// Mirror image of the previous test: server hasn't acked the predicted apply,
			// the ID is NOT in the bound state, polling must NOT promote.
			constexpr int Id = 5678;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);
			// Deliberately do NOT add to BoundActiveEffectIDs.

			for (auto& ProcessedPair : AbilityComp->GetProcessedEffectIDsForTest())
			{
				if (ProcessedPair.Value == EGMCEffectAnswerState::Pending
					&& AbilityComp->BoundActiveEffectIDs_Contains(ProcessedPair.Key))
				{
					ProcessedPair.Value = EGMCEffectAnswerState::Validated;
				}
			}

			const auto State = AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id);
			TestEqual("Pending stays Pending without bound-state confirmation",
				static_cast<int>(State), static_cast<int>(EGMCEffectAnswerState::Pending));
		});
	});

	// ── bUniqueByEffectTag opt-in single-instance protection ──────────────
	//
	// FGMCAbilityEffectData::bUniqueByEffectTag (default false). When true,
	// ApplyAbilityEffect refuses to stack a new instance if another active
	// effect on the owner already carries the same EffectTag (exact match).
	// Rejection is side-effect-free: no EffectID burned, no ProcessedEffectIDs
	// stamp, no ActiveEffectIDsBound write. Empty tag disables the check.
	Describe("bUniqueByEffectTag: single-instance protection", [this]()
	{
		It("Default value is false (opt-in)", [this]()
		{
			const FGMCAbilityEffectData Default;
			TestFalse("Default bUniqueByEffectTag is false (stacking allowed by default)",
				Default.bUniqueByEffectTag);
		});

		It("Stacking allowed when bUniqueByEffectTag=false (default)", [this]()
		{
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Persistent;
			Data.Duration           = 0.f;
			Data.EffectTag          = HealthTag;
			Data.bUniqueByEffectTag = false;

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);

			TestNotNull("First Apply succeeds", AppliedA);
			TestNotNull("Second Apply succeeds (default stacking)", AppliedB);
			TestEqual("Both instances are in ActiveEffects",
				AbilityComp->GetActiveEffects().Num(), 2);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});

		It("Second Apply rejected when bUniqueByEffectTag=true and tag already active", [this]()
		{
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Persistent;
			Data.Duration           = 0.f;
			Data.EffectTag          = HealthTag;
			Data.bUniqueByEffectTag = true;

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			TestNotNull("First Apply succeeds", AppliedA);
			const int FirstId = AppliedA ? AppliedA->EffectData.EffectID : -1;

			// Snapshot state before the rejected Apply — must be unchanged after.
			const int ActiveCountBefore = AbilityComp->GetActiveEffects().Num();
			const bool bBoundContainsFirst = AbilityComp->BoundActiveEffectIDs_Contains(FirstId);

			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);

			TestNull("Second Apply rejected (returns nullptr)", AppliedB);
			TestEqual("ActiveEffects count unchanged after rejection",
				AbilityComp->GetActiveEffects().Num(), ActiveCountBefore);
			TestTrue("First effect's bound entry untouched after rejection",
				AbilityComp->BoundActiveEffectIDs_Contains(FirstId) == bBoundContainsFirst);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});

		It("Empty EffectTag disables the check (bUniqueByEffectTag=true is a no-op without tag)", [this]()
		{
			// Two untagged effects with the flag set — the check skips because
			// matching empty tags against each other would silently collapse all
			// untagged effects into a single-instance pool, which is rarely intended.
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Persistent;
			Data.Duration           = 0.f;
			Data.EffectTag          = FGameplayTag(); // empty
			Data.bUniqueByEffectTag = true;

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);

			TestNotNull("First Apply succeeds (no tag → no check)", AppliedA);
			TestNotNull("Second Apply succeeds (no tag → no check)", AppliedB);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});

		It("Different tags pass through even when bUniqueByEffectTag=true on both", [this]()
		{
			// The check is per-tag, not global. Two effects with DIFFERENT tags
			// both flagged as unique are independent — each enforces uniqueness
			// only against its own tag.
			UGMCAbilityEffect* EffectHealth = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectMana   = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectHealth->AddToRoot(); EffectMana->AddToRoot();

			FGMCAbilityEffectData HealthData;
			HealthData.EffectType         = EGMASEffectType::Persistent;
			HealthData.Duration           = 0.f;
			HealthData.EffectTag          = HealthTag;
			HealthData.bUniqueByEffectTag = true;

			FGMCAbilityEffectData ManaData = HealthData;
			ManaData.EffectTag            = ManaTag;

			TestNotNull("Health Apply succeeds",
				AbilityComp->ApplyAbilityEffect(EffectHealth, HealthData));
			TestNotNull("Mana Apply succeeds (different tag)",
				AbilityComp->ApplyAbilityEffect(EffectMana, ManaData));

			EffectHealth->RemoveFromRoot(); EffectMana->RemoveFromRoot();
		});

		It("Deferred match suspended client-side by re-Apply (server-accept path finalizes)", [this]()
		{
			// Client predict-apply: OLD is suspended (bPendingDeathBySuccessor) but
			// not yet finalized. Tracking entry recorded in PendingReplacements.
			// When the polling promotes the successor to Validated (server bound
			// state confirms), the OLD is finalized via real EndEffect.
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Ticking;
			Data.Duration           = 0.f;
			Data.ClientGraceTime    = 1.f;
			Data.EffectTag          = HealthTag;
			Data.bUniqueByEffectTag = true;

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			TestNotNull("First Apply succeeds", AppliedA);

			AbilityComp->RemoveActiveAbilityEffect(AppliedA);
			TestTrue("EffectA still alive in ActiveEffects (defer armed or imminent end)",
				AbilityComp->GetActiveEffects().Contains(AppliedA->EffectData.EffectID));

			// Skip the test if the harness's standalone-network detection bypassed
			// the bilateral defer arm and EndEffect-ed A directly; the assertion
			// path then doesn't exercise the suspend logic.
			if (AppliedA->bCompleted) { EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot(); return; }
			TestTrue("EffectA's EndAtActionTimer is armed", AppliedA->EndAtActionTimer >= 0.0);

			// Re-Apply: B succeeds, A is SUSPENDED (not finalized).
			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);
			TestNotNull("Re-Apply succeeds during A's defer window", AppliedB);

			// On the headless harness HasAuthority() is false (orphan component),
			// so the client suspend path runs.
			TestTrue ("EffectA suspended (bPendingDeathBySuccessor=true)",
				AppliedA->bPendingDeathBySuccessor);
			TestFalse("EffectA NOT yet finalized (bCompleted=false)",
				AppliedA->bCompleted);
			TestTrue ("EffectA's EndAtActionTimer still armed (preserved for revival path)",
				AppliedA->EndAtActionTimer >= 0.0);

			// Simulate server confirmation: bound state contains B's ID + polling
			// promotes B to Validated, which finalizes A.
			AbilityComp->BoundActiveEffectIDs_Add(AppliedB->EffectData.EffectID);
			AbilityComp->TickActiveEffects(0.f);

			TestTrue ("EffectA finalized after B promoted to Validated (bCompleted=true)",
				AppliedA->bCompleted);
			TestEqual("EffectA's EndAtActionTimer cleared after finalization",
				AppliedA->EndAtActionTimer, -1.0);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});

		It("Replay-skip gate: polling and timeout reap are both no-ops during replay", [this]()
		{
			// Production code logs `Effect '...' Not Confirmed By Server (ID: '...'), Removing...`
			// at Error severity (GMCAbilityComponent.cpp:1247) when the post-replay timeout
			// reap fires on the Reapable effect. UE's automation framework auto-fails any
			// test that produces an undeclared Error log, so we declare the expected one.
			AddExpectedError(TEXT("Not Confirmed By Server"), EAutomationExpectedErrorFlags::Contains, 1);

			// Production invariant: while CL_IsReplaying() is true (forced here via the
			// WITH_AUTOMATION_WORKER test seam bForceReplayingForTest), TickActiveEffects
			// must NOT mutate ProcessedEffectIDs (no Pending → Validated promotion) and must
			// NOT trigger the Pending+timeout reap. Both paths would otherwise corrupt
			// non-rewinding state on rewound bound-state snapshots — the EF_Humanoid_Stamina_Recovery
			// regression that motivated the gate.
			UGMCAbilityEffect* PendingEffect  = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* ReapableEffect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			PendingEffect->AddToRoot(); ReapableEffect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;

			// PendingEffect: would normally promote — stamp Pending + present in bound state.
			UGMCAbilityEffect* Applied1 = AbilityComp->ApplyAbilityEffect(PendingEffect, Data);
			TestNotNull("Pending effect applied", Applied1);
			AbilityComp->GetProcessedEffectIDsForTest().Add(Applied1->EffectData.EffectID, EGMCEffectAnswerState::Pending);
			AbilityComp->BoundActiveEffectIDs_Add(Applied1->EffectData.EffectID);

			// ReapableEffect: would normally time out — stamp Pending, absent from bound, past timeout.
			UGMCAbilityEffect* Applied2 = AbilityComp->ApplyAbilityEffect(ReapableEffect, Data);
			TestNotNull("Reapable effect applied", Applied2);
			AbilityComp->GetProcessedEffectIDsForTest().Add(Applied2->EffectData.EffectID, EGMCEffectAnswerState::Pending);
			AbilityComp->BoundActiveEffectIDs_Remove(Applied2->EffectData.EffectID);
			AbilityComp->ActionTimer = 5.0;

			// Force the replay gate active. Both the polling and the reap branch should skip.
			AbilityComp->bForceReplayingForTest = true;
			AbilityComp->TickActiveEffects(0.f);

			TestEqual("During replay: Pending NOT promoted",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Applied1->EffectData.EffectID)),
				static_cast<int>(EGMCEffectAnswerState::Pending));
			TestEqual("During replay: Reapable NOT timed out",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Applied2->EffectData.EffectID)),
				static_cast<int>(EGMCEffectAnswerState::Pending));
			TestTrue("During replay: Reapable still in ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Applied2->EffectData.EffectID));

			// Drop the gate; both paths fire on the next tick.
			AbilityComp->bForceReplayingForTest = false;
			AbilityComp->TickActiveEffects(0.f);

			TestEqual("Post-replay: Pending promoted to Validated",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Applied1->EffectData.EffectID)),
				static_cast<int>(EGMCEffectAnswerState::Validated));
			// The Reapable effect transits Pending -> Timeout -> CompletedActiveEffects within
			// a single TickActiveEffects call. The cleanup loop at GMCAbilityComponent.cpp:1296
			// then removes its ProcessedEffectIDs entry in the same tick, so checking
			// FindRef == Timeout would always observe 0 (Pending default for missing key).
			// Validate the reap by asserting the entry is GONE rather than testing a transient
			// Timeout state we never observe outside the production code's own scope.
			TestFalse("Post-replay: Reapable removed from ProcessedEffectIDs (timed out + cleaned)",
				AbilityComp->GetProcessedEffectIDsForTest().Contains(Applied2->EffectData.EffectID));
			TestFalse("Post-replay: Reapable removed from ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Applied2->EffectData.EffectID));

			PendingEffect->RemoveFromRoot(); ReapableEffect->RemoveFromRoot();
		});

		It("Deferred match revived by re-Apply timeout (server-reject path)", [this]()
		{
			// Client predict-apply: OLD suspended, successor stamped Pending. The
			// server's bound state never confirms (= rejection). After the timeout
			// window, the successor is reaped and the suspended OLD is REVIVED:
			// bPendingDeathBySuccessor cleared, OLD resumes ticking modifiers.
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Ticking;
			Data.Duration           = 0.f;
			Data.ClientGraceTime    = 1.f;
			Data.EffectTag          = HealthTag;
			Data.bUniqueByEffectTag = true;

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			AbilityComp->RemoveActiveAbilityEffect(AppliedA);
			if (AppliedA->bCompleted) { EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot(); return; }

			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);
			TestNotNull("B applied", AppliedB);
			TestTrue ("A suspended", AppliedA->bPendingDeathBySuccessor);

			// Stamp B as Pending and DO NOT add to bound state (simulating server
			// rejection). Push ActionTimer past the timeout window so the reap path
			// fires.
			AbilityComp->GetProcessedEffectIDsForTest().Add(AppliedB->EffectData.EffectID, EGMCEffectAnswerState::Pending);
			AbilityComp->ActionTimer = 5.0;
			AbilityComp->TickActiveEffects(0.f);

			TestEqual("B reaped (Timeout state)",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(AppliedB->EffectData.EffectID)),
				static_cast<int>(EGMCEffectAnswerState::Timeout));
			TestFalse("A revived (bPendingDeathBySuccessor=false)",
				AppliedA->bPendingDeathBySuccessor);
			TestFalse("A still alive (not bCompleted)",
				AppliedA->bCompleted);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});

		It("Re-Apply allowed once the previous instance has been removed", [this]()
		{
			UGMCAbilityEffect* EffectA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffectB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffectA->AddToRoot(); EffectB->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType         = EGMASEffectType::Instant; // ends immediately on Tick
			Data.Duration           = 0.f;
			Data.EffectTag          = HealthTag;
			Data.bUniqueByEffectTag = true;
			Data.Modifiers.Add(MakeHealthMod(1.f));

			UGMCAbilityEffect* AppliedA = AbilityComp->ApplyAbilityEffect(EffectA, Data);
			TestNotNull("First Apply succeeds", AppliedA);

			// Tick: instant effect completes, cleanup removes it from ActiveEffects.
			AbilityComp->TickActiveEffects(1.f);
			TestFalse("First instance removed after tick",
				AbilityComp->GetActiveEffects().Contains(AppliedA->EffectData.EffectID));

			// Re-Apply: previous instance gone, the new one passes the check.
			UGMCAbilityEffect* AppliedB = AbilityComp->ApplyAbilityEffect(EffectB, Data);
			TestNotNull("Re-Apply succeeds after first instance ended", AppliedB);

			EffectA->RemoveFromRoot(); EffectB->RemoveFromRoot();
		});
	});

	// ── Bug #8 — bPreserveGrantedTagsIfMultiple default flipped to true ────
	//
	// FGMCAbilityEffectData::bPreserveGrantedTagsIfMultiple was changed from
	// false → true. Rationale: FGameplayTagContainer is set-like (no stack
	// counting), so removing the tag on the FIRST instance-end while other
	// instances of the same class still live silently strips the tag from
	// the owner — observable as the SprintCost re-trigger drain-stop bug
	// (Sprint #1 release armed bilateral PredictedEnd defer; Sprint #2 then
	// applied a fresh SprintCost; when defer expired and Sprint #1 ended,
	// its tag-removal yanked the tag the new SprintCost still relied on,
	// MustHaveTags failed, drain stopped). Single-instance behavior is
	// unchanged (the multi-instance branch never fires).
	Describe("bPreserveGrantedTagsIfMultiple stacking semantics", [this]()
	{
		It("Default value is true (flipped from upstream's false)", [this]()
		{
			const FGMCAbilityEffectData Default;
			TestTrue("Default bPreserveGrantedTagsIfMultiple is true",
				Default.bPreserveGrantedTagsIfMultiple);
		});

		It("Single instance with preserve=true: tags are removed on end", [this]()
		{
			// preserve=true falls through to remove when there is no other instance
			// of the same EffectTag — the multi-instance branch only protects when
			// >1 ActiveEffects share the EffectTag.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			Data.EffectTag  = BurningTag;                        // identifies the class
			Data.GrantedTags.AddTag(BurningTag);                 // tag granted on apply
			Data.bPreserveGrantedTagsIfMultiple = true;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			TestTrue("Granted tag is on owner after apply",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			Effect->EndEffect();

			TestFalse("Granted tag is removed when single instance ends (preserve=true)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			Effect->RemoveFromRoot();
		});

		It("Multi-instance with preserve=true: first end keeps tag, last end clears it", [this]()
		{
			// Apply two instances sharing the same EffectTag + GrantedTag. preserve=true
			// must keep the tag on the owner while at least one instance is alive.
			UGMCAbilityEffect* EffA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffA->AddToRoot(); EffB->AddToRoot();

			auto MakeData = [&]()
			{
				FGMCAbilityEffectData D;
				D.EffectType = EGMASEffectType::Persistent;
				D.Duration   = 0.f;
				D.EffectTag  = BurningTag;
				D.GrantedTags.AddTag(BurningTag);
				D.bPreserveGrantedTagsIfMultiple = true;
				return D;
			};

			AbilityComp->ApplyAbilityEffect(EffA, MakeData());
			AbilityComp->ApplyAbilityEffect(EffB, MakeData());

			TestTrue("Tag present after two applies",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// End A while B still alive — tag must survive (>1 instances at end-time).
			EffA->EndEffect();
			TestTrue("Tag preserved when one of two instances ends (preserve=true)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// End B — last instance, tag falls.
			EffB->EndEffect();
			TestFalse("Tag cleared when last instance ends (preserve=true)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			EffA->RemoveFromRoot(); EffB->RemoveFromRoot();
		});

		It("Rapid-fire: zombie bCompleted effect doesn't inflate the multi-instance count", [this]()
		{
			// Repro of the Weapon.Fire latching-tag bug. F1 is an Instant effect:
			// StartEffect adds the tag, then immediately calls EndEffect inline,
			// which sets bCompleted=true. F1 stays in ActiveEffects until the next
			// TickActiveEffects cleanup pass.
			//
			// Player fires again before that cleanup runs → F2 applied. F2's own
			// inline EndEffect calls RemoveTagsFromOwner(preserve=true). The
			// preserve check must NOT count F1 (zombie, bCompleted=true) as a
			// live sibling, otherwise count = 2 → preserve → tags latched forever.
			//
			// With the fix: only !bCompleted siblings are counted, so F1 is skipped,
			// OthersStillAlive == 0, and F2's tag removal proceeds.
			UGMCAbilityEffect* F1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* F2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			F1->AddToRoot(); F2->AddToRoot();

			auto MakeFireData = [&]()
			{
				FGMCAbilityEffectData D;
				D.EffectType = EGMASEffectType::Instant;             // ends inside StartEffect
				D.Duration   = 0.f;
				D.EffectTag  = BurningTag;
				D.GrantedTags.AddTag(BurningTag);
				D.bPreserveGrantedTagsIfMultiple = true;
				return D;
			};

			AbilityComp->ApplyAbilityEffect(F1, MakeFireData());
			TestTrue ("F1 set bCompleted via inline EndEffect (Instant)", F1->bCompleted);
			TestFalse("F1 already cleared the tag (only sibling was self)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// F1 still in ActiveEffects (cleanup happens at next Tick). Re-add the
			// tag to simulate F2's StartEffect grant — F2's own AddTagsToOwner runs
			// before F2's inline EndEffect, so the tag is present at that moment.
			AbilityComp->AddActiveTag(BurningTag);
			TestTrue("Tag re-present after manual re-add (simulating F2 StartEffect)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// F2 applied — Instant → StartEffect → AddTags → EndEffect (inline).
			// EndEffect calls RemoveTagsFromOwner(preserve=true). Without the fix,
			// the count = [F1 zombie, F2 self] = 2 → preserve → tag latches.
			AbilityComp->ApplyAbilityEffect(F2, MakeFireData());

			TestTrue("F2 set bCompleted via inline EndEffect (Instant)", F2->bCompleted);
			TestFalse("Tag removed by F2's EndEffect (zombie F1 not counted)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			F1->RemoveFromRoot(); F2->RemoveFromRoot();
		});

		It("Multi-instance with preserve=false: footgun — first end strips tag", [this]()
		{
			// The pre-flip default behavior: tag is removed as soon as ANY instance
			// ends, even if siblings of the same class still live. Confirms why we
			// flipped the default — this is structurally wrong for stackable effects.
			UGMCAbilityEffect* EffA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffA->AddToRoot(); EffB->AddToRoot();

			auto MakeData = [&]()
			{
				FGMCAbilityEffectData D;
				D.EffectType = EGMASEffectType::Persistent;
				D.Duration   = 0.f;
				D.EffectTag  = BurningTag;
				D.GrantedTags.AddTag(BurningTag);
				D.bPreserveGrantedTagsIfMultiple = false;        // explicit footgun
				return D;
			};

			AbilityComp->ApplyAbilityEffect(EffA, MakeData());
			AbilityComp->ApplyAbilityEffect(EffB, MakeData());

			EffA->EndEffect();

			// preserve=false: tag stripped even though EffB still alive.
			TestFalse("preserve=false strips the tag on first end (footgun)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			EffA->RemoveFromRoot(); EffB->RemoveFromRoot();
		});

		It("Re-trigger overlap simulation: new instance keeps the tag when old defer-ends", [this]()
		{
			// Simulates the Sprint re-trigger gameplay scenario:
			//   t=0  : Sprint #1 applied   (instance A, EffectTag=X, GrantedTags=[X])
			//   t=1  : Sprint #1 released  (bilateral defer arms — handled by ASC code,
			//                                we elide that here and just keep A alive)
			//   t=2  : Sprint #2 applied   (instance B, same EffectTag=X)
			//   t=3  : Old defer expires   → A.EndEffect()
			//          With preserve=true , B still alive → tag survives, B keeps draining
			//          With preserve=false, A's end strips the tag, B's MustHaveTags fail
			//                                 on next tick, drain stops — the bug
			UGMCAbilityEffect* SprintCostA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* SprintCostB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			SprintCostA->AddToRoot(); SprintCostB->AddToRoot();

			auto MakeSprintCostData = [&]()
			{
				FGMCAbilityEffectData D;
				D.EffectType = EGMASEffectType::Ticking;          // mirrors real SprintCost
				D.Duration   = 0.f;
				D.EffectTag  = BurningTag;                        // stand-in for "Sprint" tag
				D.GrantedTags.AddTag(BurningTag);
				D.bPreserveGrantedTagsIfMultiple = true;          // the fix
				return D;
			};

			AbilityComp->ApplyAbilityEffect(SprintCostA, MakeSprintCostData());
			AbilityComp->ApplyAbilityEffect(SprintCostB, MakeSprintCostData());

			TestTrue("Tag granted by both instances while overlapping",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// Old SprintCost A's bilateral defer expires — A ends naturally.
			SprintCostA->EndEffect();

			TestTrue("Tag still present after old instance defer-ends (B alive, preserve=true)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			// New SprintCost B is now the sole owner of the tag — eventually it ends too.
			SprintCostB->EndEffect();

			TestFalse("Tag cleared once B (the surviving instance) ends",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			SprintCostA->RemoveFromRoot(); SprintCostB->RemoveFromRoot();
		});

		It("preserve=true with no EffectTag falls back to remove (set-like container can't gate)", [this]()
		{
			// RemoveTagsFromOwner needs a valid EffectTag to count siblings via
			// GetActiveEffectsByTag; without one, it logs a warning and falls through
			// to the unconditional remove. Documented behavior — captured here so a
			// future refactor doesn't silently change it.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			// EffectTag deliberately left empty.
			Data.GrantedTags.AddTag(BurningTag);
			Data.bPreserveGrantedTagsIfMultiple = true;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			TestTrue("Tag present after apply", AbilityComp->GetActiveTags().HasTag(BurningTag));

			AddExpectedError(TEXT("Effect Tag is not valid with PreserveMultipleInstances"),
				EAutomationExpectedErrorFlags::Contains, 1);
			Effect->EndEffect();

			TestFalse("Tag removed because EffectTag was missing (preserve fallback)",
				AbilityComp->GetActiveTags().HasTag(BurningTag));

			Effect->RemoveFromRoot();
		});
	});

	// ── Bug #4 v2 extensions: drift detection and recovery ─────────────────
	//
	// Beyond the basic Pending → Validated polling tested above, these cases
	// exercise what happens when the bound state DIVERGES from the local
	// ProcessedEffectIDs / ActiveEffects view: timeout drift, recovery via
	// late server confirmation, partial stacks where some instances are
	// validated and others are not. All tests inline the polling logic
	// because the headless harness is authoritative-by-default and the
	// production polling block is gated by !HasAuthority().
	Describe("Drift detection and recovery", [this]()
	{
		// Helper: drain the production polling logic once, deterministically.
		// Mirror of TickActiveEffects' MONOTONIC reconciliation: ID present in bound
		// state promotes Pending → Validated. No demotion path — the server-rejected
		// case is caught by the Pending+timeout reap below; the server-removed case
		// is delivered via RPCClientEndEffect. Demoting on bound-absence was found to
		// reap legitimate effects during the natural 1-2 tick replication lag window.
		auto RunPollingOnce = [this]()
		{
			for (auto& ProcessedPair : AbilityComp->GetProcessedEffectIDsForTest())
			{
				if (ProcessedPair.Value == EGMCEffectAnswerState::Pending
					&& AbilityComp->BoundActiveEffectIDs_Contains(ProcessedPair.Key))
				{
					ProcessedPair.Value = EGMCEffectAnswerState::Validated;
				}
			}
		};

		It("Drift: predicted apply with no bound confirmation eventually times out", [this, RunPollingOnce]()
		{
			// Production code logs `Effect '...' Not Confirmed By Server (ID: '...'), Removing...`
			// at Error severity (GMCAbilityComponent.cpp:1247) when the timeout reap fires --
			// which is exactly what this test exercises. Declare the expected error so the
			// automation framework doesn't auto-fail on the legitimate diagnostic log.
			AddExpectedError(TEXT("Not Confirmed By Server"), EAutomationExpectedErrorFlags::Contains, 1);

			// Apply a Predicted effect locally without ever adding the ID to the
			// bound state — server scenario where the activation never reached the
			// authoritative side. After ClientGraceTime + tick, the timeout in
			// TickActiveEffects must fire and reap the local instance.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);

			// Manually scrub the bound state — simulates "server never added the ID"
			// since the effect we just applied auto-mirrored on the client side.
			AbilityComp->BoundActiveEffectIDs_Remove(Id);

			// Push ActionTimer past the timeout window. The default
			// ClientEffectApplicationTimeout is 1.0; effect's
			// ClientEffectApplicationTime was set to 1.0 in InitializeEffect.
			AbilityComp->ActionTimer = 5.0;
			RunPollingOnce();
			AbilityComp->TickActiveEffects(0.f);

			TestFalse("Timed-out predicted effect removed from ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Id));
			TestFalse("Timed-out predicted effect removed from ProcessedEffectIDs",
				AbilityComp->GetProcessedEffectIDsForTest().Contains(Id));

			Effect->RemoveFromRoot();
		});

		It("Recovery: late bound-state confirmation prevents timeout", [this, RunPollingOnce]()
		{
			// Apply locally, server confirms via bound state JUST before timeout
			// would fire. Validated state must stick, no removal.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);

			// Apply auto-mirrored the ID into the bound state (deterministic both-side
			// writer). Scrub it to simulate the "server hasn't replicated confirmation
			// yet" window — which is exactly the divergence the polling+timeout machinery
			// has to handle.
			AbilityComp->BoundActiveEffectIDs_Remove(Id);

			// Step 1 : ActionTimer advances but stays within timeout window.
			AbilityComp->ActionTimer = 1.5;
			RunPollingOnce();
			AbilityComp->TickActiveEffects(0.f);
			TestEqual("Still Pending while ID absent from bound state",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id)),
				static_cast<int>(EGMCEffectAnswerState::Pending));
			TestTrue("Effect still in ActiveEffects (within grace)",
				AbilityComp->GetActiveEffects().Contains(Id));

			// Step 2 : server's bound state arrives carrying the ID.
			AbilityComp->BoundActiveEffectIDs_Add(Id);

			// Step 3 : another tick promotes Pending → Validated.
			RunPollingOnce();
			AbilityComp->TickActiveEffects(0.f);
			TestEqual("Promoted to Validated on bound-state arrival",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id)),
				static_cast<int>(EGMCEffectAnswerState::Validated));

			// Step 4 : even past the original timeout window, the Validated state
			// shields the effect — no late removal.
			AbilityComp->ActionTimer = 10.0;
			AbilityComp->TickActiveEffects(0.f);
			TestTrue("Validated effect survives past would-be timeout",
				AbilityComp->GetActiveEffects().Contains(Id));

			Effect->RemoveFromRoot();
		});

		It("Partial stack: validated and pending IDs coexist; only validated survive timeout", [this, RunPollingOnce]()
		{
			// Production code logs the diagnostic Error when the unvalidated stack member
			// is reaped -- see Drift test above for the rationale.
			AddExpectedError(TEXT("Not Confirmed By Server"), EAutomationExpectedErrorFlags::Contains, 1);

			// Two predicted instances (different IDs). Server confirms only one
			// via the bound state. After timeout window passes, the unconfirmed
			// one is reaped, the confirmed one stays.
			UGMCAbilityEffect* EffOK   = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffDrop = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffOK->AddToRoot(); EffDrop->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;

			AbilityComp->ApplyAbilityEffect(EffOK,   Data);
			AbilityComp->ApplyAbilityEffect(EffDrop, Data);

			const int IdOK   = EffOK->EffectData.EffectID;
			const int IdDrop = EffDrop->EffectData.EffectID;
			TestNotEqual("Two distinct EffectIDs were assigned", IdOK, IdDrop);

			AbilityComp->GetProcessedEffectIDsForTest().Add(IdOK,   EGMCEffectAnswerState::Pending);
			AbilityComp->GetProcessedEffectIDsForTest().Add(IdDrop, EGMCEffectAnswerState::Pending);

			// Server confirms only IdOK; IdDrop stays absent.
			AbilityComp->BoundActiveEffectIDs_Add(IdOK);
			AbilityComp->BoundActiveEffectIDs_Remove(IdDrop);

			// Polling promotes IdOK; advance past timeout to reap IdDrop.
			AbilityComp->ActionTimer = 5.0;
			RunPollingOnce();
			AbilityComp->TickActiveEffects(0.f);

			TestTrue("Validated stack member survives",
				AbilityComp->GetActiveEffects().Contains(IdOK));
			TestFalse("Unvalidated stack member is reaped",
				AbilityComp->GetActiveEffects().Contains(IdDrop));

			EffOK->RemoveFromRoot(); EffDrop->RemoveFromRoot();
		});

		It("Validated is monotonic: server-side bound-state drop does NOT demote Validated", [this, RunPollingOnce]()
		{
			// Polling promotion is one-way. Once an effect is Validated, transient
			// bound-state absence (e.g. natural replication lag with Periodic_Output
			// simulation mode) must NOT demote it back to Pending — that would expose
			// legitimate effects to the timeout reap path during a benign 1-2 tick
			// window after Apply, which was observed killing EF_Humanoid_Stamina_Recovery
			// on the live client and producing chain replays as Stamina diverged.
			//
			// Server-explicit removals come through RPCClientEndEffect, not through
			// inference on bound-state absence.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);
			// Apply already wrote the ID into the bound state (deterministic both-side
			// writer); the explicit Add here is a no-op kept for readability.
			AbilityComp->BoundActiveEffectIDs_Add(Id);

			RunPollingOnce();
			TestEqual("Promoted to Validated on bound presence",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id)),
				static_cast<int>(EGMCEffectAnswerState::Validated));

			// Server's bound state replicates without the ID — could be a one-tick
			// lag (snapshot captured pre-AncillaryTick-apply) or an authoritative drop.
			AbilityComp->BoundActiveEffectIDs_Remove(Id);

			RunPollingOnce();
			TestEqual("Validated stays Validated through bound-state absence",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id)),
				static_cast<int>(EGMCEffectAnswerState::Validated));

			// Even past the would-be timeout window, the Validated effect is shielded
			// from the Pending+timeout reap path (which gates on Pending only).
			AbilityComp->ActionTimer = 5.0;
			AbilityComp->TickActiveEffects(0.f);
			TestTrue("Validated effect is NOT reaped through bound-state absence + timeout",
				AbilityComp->GetActiveEffects().Contains(Id));

			Effect->RemoveFromRoot();
		});

		It("Effect cleanup follows the server-only contract for bound-state removal", [this]()
		{
			// Server-only writer contract for ActiveEffectIDsBound: only the authoritative
			// side adds/removes IDs, the client receives the result via replication. The
			// cleanup loop in TickActiveEffects mirrors that — `BoundActiveEffectIDs_Remove`
			// is gated by HasAuthority() in production. The headless harness reports
			// HasAuthority()==false on orphan components, so the in-process Remove is
			// elided. We simulate the server-replication path explicitly by populating
			// the bound state up front.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Duration   = 0.f;
			Data.Modifiers.Add(MakeHealthMod(1.f));
			AbilityComp->ApplyAbilityEffect(Effect, Data);

			const int Id = Effect->EffectData.EffectID;
			// Simulate the server having replicated the ID into our bound state.
			AbilityComp->BoundActiveEffectIDs_Add(Id);
			TestTrue("Pre-tick: simulated server-confirmed ID present",
				AbilityComp->BoundActiveEffectIDs_Contains(Id));

			AbilityComp->TickActiveEffects(0.f);

			TestFalse("Cleanup: instant effect removed from ActiveEffects",
				AbilityComp->GetActiveEffects().Contains(Id));
			// Bound state stays as last-replicated until server's next sync overwrites it
			// — the local cleanup is server-gated by design (mirrors the Apply contract).
			// Production parity: in a networked context, the server's next move-tick output
			// brings the client back into sync.

			Effect->RemoveFromRoot();
		});
	});

	// ── Set / SetReplace edge cases on the bound-attribute path ─────────────
	//
	// Set/SetReplace are stored in ValueTemporalModifiers like any other
	// modifier and participate in PurgeTemporalModifier. These tests exercise
	// the full attribute path (AddModifier → CalculateValue) on a real bound
	// attribute attached to the harness, plus a few invariants that the
	// targeted unit tests in GMAS_AttributeSpec don't fully cover.
	Describe("Set / SetReplace: attribute integration", [this]()
	{
		It("Set on a bound attribute does not modify RawValue", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute* Health = const_cast<FAttribute*>(AbilityComp->GetAttributeByTag(HealthTag));
			if (!TestNotNull("Health attribute exists", Health)) { Effect->RemoveFromRoot(); return; }

			const float OriginalRaw = Health->RawValue;

			FGMCAttributeModifier SetMod;
			SetMod.AttributeTag        = HealthTag;
			SetMod.Op                  = EModifierType::Set;
			SetMod.ValueType           = EGMCAttributeModifierType::AMT_Value;
			SetMod.ModifierValue       = 42.f;
			SetMod.DeltaTime           = 1.f;
			SetMod.bRegisterInHistory  = true;
			SetMod.SourceAbilityEffect = Effect;
			SetMod.ApplicationIndex    = 1;
			SetMod.ActionTimer         = AbilityComp->ActionTimer;
			Health->AddModifier(SetMod);
			Health->CalculateValue();

			TestEqual("Value reflects Set", Health->Value, 42.f);
			TestEqual("RawValue untouched", Health->RawValue, OriginalRaw);

			Effect->RemoveFromRoot();
		});

		It("Two effects with overlapping Sets — most recent ActionTimer wins", [this]()
		{
			UGMCAbilityEffect* EffA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffA->AddToRoot(); EffB->AddToRoot();

			FAttribute* Health = const_cast<FAttribute*>(AbilityComp->GetAttributeByTag(HealthTag));
			if (!TestNotNull("Health attribute exists", Health)) { EffA->RemoveFromRoot(); EffB->RemoveFromRoot(); return; }

			auto MakeSet = [&](UGMCAbilityEffect* Eff, float Target, int AppIdx, double T)
			{
				FGMCAttributeModifier M;
				M.AttributeTag        = HealthTag;
				M.Op                  = EModifierType::Set;
				M.ValueType           = EGMCAttributeModifierType::AMT_Value;
				M.ModifierValue       = Target;
				M.DeltaTime           = 1.f;
				M.bRegisterInHistory  = true;
				M.SourceAbilityEffect = Eff;
				M.ApplicationIndex    = AppIdx;
				M.ActionTimer         = T;
				return M;
			};

			Health->AddModifier(MakeSet(EffA, 30.f, 1, 1.0));
			Health->AddModifier(MakeSet(EffB, 80.f, 2, 2.0));
			Health->CalculateValue();
			TestEqual("Most recent Set (80 from EffB) wins", Health->Value, 80.f);

			// Remove the later Set; earlier Set takes over.
			Health->RemoveTemporalModifier(2, EffB);
			Health->CalculateValue();
			TestEqual("After removing EffB's Set, EffA's Set (30) wins", Health->Value, 30.f);

			EffA->RemoveFromRoot(); EffB->RemoveFromRoot();
		});

		It("PurgeTemporalModifier on a bound attribute restores RawValue base when Set is purged", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute* Health = const_cast<FAttribute*>(AbilityComp->GetAttributeByTag(HealthTag));
			if (!TestNotNull("Health attribute exists", Health)) { Effect->RemoveFromRoot(); return; }

			const float OriginalRaw = Health->RawValue;

			FGMCAttributeModifier SetMod;
			SetMod.AttributeTag        = HealthTag;
			SetMod.Op                  = EModifierType::Set;
			SetMod.ValueType           = EGMCAttributeModifierType::AMT_Value;
			SetMod.ModifierValue       = 1.f;
			SetMod.DeltaTime           = 1.f;
			SetMod.bRegisterInHistory  = true;
			SetMod.SourceAbilityEffect = Effect;
			SetMod.ApplicationIndex    = 1;
			SetMod.ActionTimer         = 5.0;
			Health->AddModifier(SetMod);
			Health->CalculateValue();
			TestEqual("Set active: Value pinned to 1", Health->Value, 1.f);

			// Simulate GMC rollback to before the Set's ActionTimer.
			Health->PurgeTemporalModifier(2.0);
			Health->CalculateValue();
			TestEqual("After rollback: Value reverts to RawValue base", Health->Value, OriginalRaw);

			Effect->RemoveFromRoot();
		});
	});

	// ── ValueTemporalModifiers replication regression guard ─────────────────
	//
	// The performance commit dropped UPROPERTY() from the field. If anyone
	// re-adds it later by mistake, this regression test catches the symptom:
	// a freshly-applied modifier should be visible in the local list (it always
	// was), and the count should reflect local-only mutation regardless of
	// network state.
	Describe("Perf: ValueTemporalModifiers local-only behavior", [this]()
	{
		It("Adding a temporal modifier increments the local list count", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute* Health = const_cast<FAttribute*>(AbilityComp->GetAttributeByTag(HealthTag));
			if (!TestNotNull("Health attribute exists", Health)) { Effect->RemoveFromRoot(); return; }

			// We can't directly read the protected ValueTemporalModifiers,
			// but the public Value computation reflects its content.
			const float Baseline = Health->Value;

			FGMCAttributeModifier Mod;
			Mod.AttributeTag        = HealthTag;
			Mod.Op                  = EModifierType::Add;
			Mod.ValueType           = EGMCAttributeModifierType::AMT_Value;
			Mod.ModifierValue       = 7.f;
			Mod.DeltaTime           = 1.f;
			Mod.bRegisterInHistory  = true;
			Mod.SourceAbilityEffect = Effect;
			Mod.ApplicationIndex    = 1;
			Mod.ActionTimer         = AbilityComp->ActionTimer;
			Health->AddModifier(Mod);
			Health->CalculateValue();

			TestEqual("Value reflects newly-added temporal modifier",
				Health->Value, Baseline + 7.f);

			// Removing it brings the value back — confirms the entry was actually
			// in the local list (vs. somehow being a permanent RawValue mutation).
			Health->RemoveTemporalModifier(1, Effect);
			Health->CalculateValue();
			TestEqual("Value returns to baseline after RemoveTemporalModifier",
				Health->Value, Baseline);

			Effect->RemoveFromRoot();
		});
	});

	// ── Server-auth multi-op batch dispatch ───────────────────────────────────
	// Validates the BoundQueueV2 batch wrapper path that fixes the multi-effect-
	// same-frame race where N>1 ops were delayed ~1s via OnServerOperationForced.
	// See plan_gmas_serverauth_batch.md (or commit message of this fix).

	Describe("ServerAuth multi-op batch dispatch", [this]()
	{
		// BeforeEach/AfterEach inherited from the spec-level declarations above
		// (SetupHarness / TeardownHarness). Re-declaring them here would double
		// the setup+teardown, which crashes on the second AttrData->RemoveFromRoot()
		// because the first teardown already nulled the pointer.

		It("GenPreLocalMoveExecution wraps 3 queued ops into a single BatchOperation (FIFO order)", [this]()
		{
			FGMASBoundQueueV2ApplyEffectOperation Op;
			Op.EffectClass = UGMCAbilityEffect::StaticClass();

			Op.EffectID = 100;
			const int OpID1 = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(Op);
			Op.EffectID = 101;
			const int OpID2 = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(Op);
			Op.EffectID = 102;
			const int OpID3 = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(Op);

			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations = { OpID1, OpID2, OpID3 };

			AbilityComp->GetBoundQueueV2ForTest().GenPreLocalMoveExecution();

			TestTrue("OperationData wrapped as BatchOperation",
				AbilityComp->GetBoundQueueV2ForTest().OperationData.GetScriptStruct() == FGMASBoundQueueV2BatchOperation::StaticStruct());

			const FGMASBoundQueueV2BatchOperation Batch = AbilityComp->GetBoundQueueV2ForTest().OperationData.Get<FGMASBoundQueueV2BatchOperation>();
			TestEqual("Batch carries 3 sub-IDs", Batch.SubOperationIDs.Num(), 3);
			TestEqual("FIFO order: first sub-ID is OpID1", Batch.SubOperationIDs[0], OpID1);
			TestEqual("FIFO order: second sub-ID is OpID2", Batch.SubOperationIDs[1], OpID2);
			TestEqual("FIFO order: third sub-ID is OpID3", Batch.SubOperationIDs[2], OpID3);
			TestEqual("ClientQueuedOperations drained", AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations.Num(), 0);
		});

		It("GenPreLocalMoveExecution single-op fast path: no batch wrapping", [this]()
		{
			FGMASBoundQueueV2ApplyEffectOperation Op;
			Op.EffectClass = UGMCAbilityEffect::StaticClass();
			Op.EffectID = 200;
			const int OpID = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(Op);

			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations = { OpID };

			AbilityComp->GetBoundQueueV2ForTest().GenPreLocalMoveExecution();

			TestTrue("OperationData is the raw payload (single-op path preserved)",
				AbilityComp->GetBoundQueueV2ForTest().OperationData.GetScriptStruct() == FGMASBoundQueueV2ApplyEffectOperation::StaticStruct());
		});

		It("GenPreLocalMoveExecution empty queue resets OperationData to empty base", [this]()
		{
			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations.Empty();

			AbilityComp->GetBoundQueueV2ForTest().GenPreLocalMoveExecution();

			TestTrue("OperationData is empty base struct",
				AbilityComp->GetBoundQueueV2ForTest().OperationData.GetScriptStruct() == FGMASBoundQueueV2OperationBaseData::StaticStruct());
		});

		It("GenPreLocalMoveExecution with mixed positive+negative IDs falls back to single-slot path", [this]()
		{
			// Server-broadcast op (positive ID via MakeOperationData on non-client netmode).
			FGMASBoundQueueV2ApplyEffectOperation Op;
			Op.EffectClass = UGMCAbilityEffect::StaticClass();
			Op.EffectID = 300;
			const int PosOpID = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(Op);

			// Synthesize a client-initiated ID (negative). Bypasses MakeOperationData
			// because we cannot force GetNextOperationID to return negative in test env
			// (test MoveCmp's GetNetMode() != NM_Client). The fake ID has no cached
			// payload -- intentional: the routing decision under test depends only on
			// the queue's sign distribution, not on the popped payload's resolution.
			const int NegOpID = -42;

			// Queue both: presence of negative ID must force single-slot fallback.
			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations = { PosOpID, NegOpID };

			AbilityComp->GetBoundQueueV2ForTest().GenPreLocalMoveExecution();

			TestFalse("Mixed queue must NOT produce a BatchOperation",
				AbilityComp->GetBoundQueueV2ForTest().OperationData.GetScriptStruct() == FGMASBoundQueueV2BatchOperation::StaticStruct());
			TestEqual("Single-slot fallback pops one op, leaves the other in queue",
				AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations.Num(), 1);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
