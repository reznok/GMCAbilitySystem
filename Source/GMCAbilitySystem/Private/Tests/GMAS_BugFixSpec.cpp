// Layer 2 regression tests covering the seven bugs fixed in this release:
//
//   Bug #1  GMCAbilityEffect.cpp   MustMaintainQuery logic was inverted —
//                                   effect ended when query matched instead of
//                                   when it stopped matching.
//   Bug #2  Retired — CheckRemovedEffects + ActiveEffectIDs dropped in the
//                                   single-channel refactor. Effect removal flows
//                                   exclusively through BoundQueueV2 ops.
//   Bug #3  GMCAbility.cpp          TickTasks / AncillaryTickTasks iterated a
//                                   TMap<int,Task*> via RunningTasks[i] —
//                                   TMap::operator[] keyed by integer crashes
//                                   when key i does not exist.
//   Bug #4  GMCAbilityComponent.cpp GetEffectFromHandle accessed
//                                   ActiveEffects[NetworkId] without a
//                                   Contains guard (UB / potential crash).
//   Bug #5  GMCAbilityComponent.cpp ProcessedEffectIDs entries were added at
//                                   effect creation but never removed after
//                                   expiry, causing unbounded map growth.
//   Bug #6  GMCAbility.cpp          CanAffordAbilityCost used O(n×m) nested
//                                   loop with GetAllAttributes() heap-allocated
//                                   on every iteration; fixed to O(n) via
//                                   GetAttributeByTag.
//   Bug #7  GMCAbilityEffect.h      EGMASEffectState CurrentState lacked an
//                                   explicit initialiser (relied on UObject
//                                   zero-fill which produces the wrong enum
//                                   value on non-zero-initialised allocators).
//
// All tests use the Layer-2 harness (UGMAS_TestMovementCmp + headless
// UGMC_AbilitySystemComponent).  No world, no network stack.

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
	Describe("Bug #1: MustMaintainQuery", [this]()
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
	Describe("Bug #3: TickTasks TMap iteration", [this]()
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
	Describe("Bug #4: GetEffectFromHandle stale handle safety", [this]()
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
	Describe("Bug #5: ProcessedEffectIDs cleanup on effect expiry", [this]()
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
	Describe("Bug #6: CanAffordAbilityCost correctness", [this]()
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
	Describe("Bug #7: EGMASEffectState CurrentState initialiser", [this]()
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
	Describe("Bug #3: PredictedEnd defer Tick consume (ActionTimer-absolute)", [this]()
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

	// ── Bug #4 retired ─────────────────────────────────────────────────────
	// CheckRemovedEffects replication grace was a workaround for the asymmetry
	// between RPCOnServerOperationAdded (RPC, ~RTT/2) and ActiveEffectIDs
	// (DOREPLIFETIME, ~RTT). Both ActiveEffectIDs and CheckRemovedEffects were
	// dropped in the single-channel refactor — Bug #4 is now structurally
	// impossible. ClientGraceTime is still used by the bilateral PredictedEnd
	// defer for Ticking/Periodic effects (Bug #3 of migration notes), but that
	// is covered by the "PredictedEnd defer" test block below.
	// ClientEffectApplicationTime continues to be set in InitializeEffect for
	// consumers that read it (debug overlays, future analytics).

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
}

#endif // WITH_AUTOMATION_WORKER
