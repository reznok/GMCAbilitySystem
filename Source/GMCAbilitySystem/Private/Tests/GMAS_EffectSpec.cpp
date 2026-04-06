// Layer 2 tests: effect processing through UGMC_AbilitySystemComponent.
//
// Harness wires a UGMAS_TestMovementCmp (GetNetMode → NM_Standalone) to a fresh
// UGMC_AbilitySystemComponent, calls BindReplicationData() to initialise attributes,
// then drives GenPredictionTick() to simulate the prediction phase without a real
// GMC movement loop or network stack.
//
// Limitations at this layer (covered in Layer 3):
//   • Duration-based expiry — ActionTimer stays at -1.0 because GetMoveTimestamp()
//     on the stub always returns MoveMetaData.Timestamp = -1.0 (default).
//     Effects are removed explicitly where needed.
//   • Periodic effects — depend on ActionTimer advancing.
//   • Rollback / replay scenarios.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "UGMAS_TestMovementCmp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASEffectSpec,
	"GMAS.Unit.Effect",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp    = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;
	FGameplayTag HealthTag;
	FGameplayTag BurningTag;

	// Create and wire a fresh ability component with a Health attribute [0,200], init=100.
	void SetupHarness();
	void TeardownHarness();

	// Return a modifier that targets Health.
	FGMCAttributeModifier MakeHealthMod(float Amount) const;

END_DEFINE_SPEC(FGMASEffectSpec)

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

void FGMASEffectSpec::SetupHarness()
{
	// One-time tag registration.  FNativeGameplayTag's constructor calls
	// UGameplayTagsManager::AddNativeGameplayTag(FNativeGameplayTag*), which
	// has no bDoneAddingNativeTags guard — unlike AddNativeGameplayTag(FName),
	// it is safe to call after editor startup from test code.
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Health"), TEXT("Health for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Burning"), TEXT("Burning for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag  = SHealthTag.GetTag();
	BurningTag = SBurningTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData HealthData;
	HealthData.AttributeTag = HealthTag;
	HealthData.DefaultValue = 100.f;
	HealthData.Clamp.Min    = 0.f;
	HealthData.Clamp.Max    = 200.f;
	HealthData.bGMCBound    = true;
	AttrData->AttributeData.Add(HealthData);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;

	// Initialises BoundAttributes from AttrData and wires BoundQueueV2 to MoveCmp.
	// GMC BindSinglePrecisionFloat / BindGameplayTagContainer etc. add internal
	// binding descriptors on the stub component — benign with no active move loop.
	AbilityComp->BindReplicationData();

	// Seed ActionTimer to a non-zero value so GetNextAvailableEffectID() works
	// before the first GenPredictionTick.  -1.0 matches MoveMetaData.Timestamp's
	// default value, so ActionTimer stays stable across GenPredictionTick calls
	// (the stub always returns -1.0 from GetMoveTimestamp()).
	AbilityComp->ActionTimer = -1.0;
}

void FGMASEffectSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASEffectSpec::MakeHealthMod(float Amount) const
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

void FGMASEffectSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── Instant Effects ─────────────────────────────────────────────────────────
	Describe("Instant effect", [this]()
	{
		It("applies modifier to RawValue and completes on the first tick", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Modifiers.Add(MakeHealthMod(25.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			// Effect is synchronously completed inside InitializeEffect → StartEffect → EndEffect.
			// GenPredictionTick drives ProcessAttributes so Value is recalculated.
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue == 125 after instant +25", AbilityComp->GetAttributeRawValue(HealthTag), 125.f);
			TestEqual("Value == 125 after instant +25",    AbilityComp->GetAttributeValueByTag(HealthTag), 125.f);
			TestTrue ("Effect removed from ActiveEffects", AbilityComp->GetActiveEffects().IsEmpty());

			Effect->RemoveFromRoot();
		});

		It("subtracts from RawValue for damage", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Modifiers.Add(MakeHealthMod(-40.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue == 60 after instant -40", AbilityComp->GetAttributeRawValue(HealthTag), 60.f);

			Effect->RemoveFromRoot();
		});

		It("clamps RawValue to the attribute Max", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Modifiers.Add(MakeHealthMod(150.f)); // 100 + 150 = 250, clamped to 200

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue clamped to Max=200", AbilityComp->GetAttributeRawValue(HealthTag), 200.f);
			TestEqual("Value   clamped to Max=200",  AbilityComp->GetAttributeValueByTag(HealthTag), 200.f);

			Effect->RemoveFromRoot();
		});

		It("clamps RawValue to the attribute Min", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.Modifiers.Add(MakeHealthMod(-999.f)); // would go negative, clamped to 0

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue clamped to Min=0", AbilityComp->GetAttributeRawValue(HealthTag), 0.f);

			Effect->RemoveFromRoot();
		});
	});

	// ── Persistent (reversible) Effects ─────────────────────────────────────────
	Describe("Persistent reversible effect (bNegateEffectAtEnd=true)", [this]()
	{
		It("adds temporal modifier to Value without changing RawValue", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true; // temporal — stored in ValueTemporalModifiers
			Data.Duration         = 0.f;    // infinite (duration expiry not tested at Layer 2)
			Data.Modifiers.Add(MakeHealthMod(30.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue unchanged at 100",          AbilityComp->GetAttributeRawValue(HealthTag), 100.f);
			TestEqual("Value includes temporal buff → 130", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);

			Effect->RemoveFromRoot();
		});

		It("reverts Value to RawValue when removed", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration         = 0.f;
			Data.Modifiers.Add(MakeHealthMod(30.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);
			TestEqual("Value buffed to 130 before removal", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);

			AbilityComp->RemoveActiveAbilityEffect(Effect);
			// A zero-DeltaTime tick drives ProcessAttributes without ticking effects.
			AbilityComp->GenPredictionTick(0.f);
			TestEqual("Value reverts to 100 after removal", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			Effect->RemoveFromRoot();
		});

		It("stacks multiple persistent buffs additively", [this]()
		{
			UGMCAbilityEffect* Eff1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Eff2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Eff1->AddToRoot(); Eff2->AddToRoot();

			auto MakeData = [&](float Amt) {
				FGMCAbilityEffectData D;
				D.EffectType        = EGMASEffectType::Persistent;
				D.bNegateEffectAtEnd = true;
				D.Duration          = 0.f;
				D.Modifiers.Add(MakeHealthMod(Amt));
				return D;
			};

			AbilityComp->ApplyAbilityEffect(Eff1, MakeData(20.f));
			AbilityComp->ApplyAbilityEffect(Eff2, MakeData(15.f));
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("100 + 20 + 15 = 135", AbilityComp->GetAttributeValueByTag(HealthTag), 135.f);

			Eff1->RemoveFromRoot(); Eff2->RemoveFromRoot();
		});
	});

	// ── Ticking Effects ──────────────────────────────────────────────────────────
	Describe("Ticking effect", [this]()
	{
		It("accumulates RawValue by modifier × DeltaTime each prediction tick", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Ticking;
			Data.Duration   = 0.f; // infinite
			Data.Modifiers.Add(MakeHealthMod(10.f)); // 10 HP/s

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f); // +10
			AbilityComp->GenPredictionTick(1.f); // +10
			AbilityComp->GenPredictionTick(1.f); // +10

			TestNearlyEqual("RawValue = 130 after 3 ticks × 10 HP/s",
				AbilityComp->GetAttributeRawValue(HealthTag), 130.f, KINDA_SMALL_NUMBER);

			Effect->RemoveFromRoot();
		});

		It("scales correctly with sub-second DeltaTime", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Ticking;
			Data.Duration   = 0.f;
			Data.Modifiers.Add(MakeHealthMod(60.f)); // 60 HP/s

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			for (int i = 0; i < 60; i++)
			{
				AbilityComp->GenPredictionTick(1.f / 60.f);
			}
			// 60 × (60 × 1/60) = 60 HP total gain
			TestNearlyEqual("RawValue ≈ 160 after 1 second at 60 hz",
				AbilityComp->GetAttributeRawValue(HealthTag), 160.f, 0.01f);

			Effect->RemoveFromRoot();
		});
	});

	// ── Tag Granting ─────────────────────────────────────────────────────────────
	Describe("Tag granting", [this]()
	{
		It("grants tags on effect start and removes them on effect end", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			Data.GrantedTags.AddTag(BurningTag);

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestTrue("Status.Burning active while effect is running",
				AbilityComp->HasActiveTag(BurningTag));

			AbilityComp->RemoveActiveAbilityEffect(Effect);
			AbilityComp->GenPredictionTick(0.f);

			TestFalse("Status.Burning removed after effect ends",
				AbilityComp->HasActiveTag(BurningTag));

			Effect->RemoveFromRoot();
		});

		It("does not grant tags to instant effects (they end immediately)", [this]()
		{
			// Instant effects complete and EndEffect() runs inside StartEffect() before
			// the granted tags would be readable.  Tags ARE briefly added then removed.
			// The observable state after a tick should have no tags.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.GrantedTags.AddTag(BurningTag);

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestFalse("No lingering tags after instant effect completes",
				AbilityComp->HasActiveTag(BurningTag));

			Effect->RemoveFromRoot();
		});
	});

	// ── Application Conditions ───────────────────────────────────────────────────
	Describe("Application conditions", [this]()
	{
		It("blocks the effect when ApplicationMustHaveTags are not present", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Instant;
			Data.ApplicationMustHaveTags.AddTag(BurningTag); // owner lacks this
			Data.Modifiers.Add(MakeHealthMod(-50.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue unchanged when ApplicationMustHaveTags not met",
				AbilityComp->GetAttributeRawValue(HealthTag), 100.f);

			Effect->RemoveFromRoot();
		});

		It("blocks the effect when ApplicationMustNotHaveTags are present", [this]()
		{
			UGMCAbilityEffect* TagGrantor = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Blocked    = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			TagGrantor->AddToRoot(); Blocked->AddToRoot();

			// Grant the Burning tag via a persistent effect.
			FGMCAbilityEffectData GrantData;
			GrantData.EffectType = EGMASEffectType::Persistent;
			GrantData.Duration   = 0.f;
			GrantData.GrantedTags.AddTag(BurningTag);
			AbilityComp->ApplyAbilityEffect(TagGrantor, GrantData);
			AbilityComp->GenPredictionTick(1.f);
			TestTrue("Burning is active", AbilityComp->HasActiveTag(BurningTag));

			// Attempt an effect that must NOT have Burning — should be blocked.
			FGMCAbilityEffectData BlockedData;
			BlockedData.EffectType = EGMASEffectType::Instant;
			BlockedData.ApplicationMustNotHaveTags.AddTag(BurningTag);
			BlockedData.Modifiers.Add(MakeHealthMod(-50.f));
			AbilityComp->ApplyAbilityEffect(Blocked, BlockedData);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("Damage blocked by ApplicationMustNotHaveTags",
				AbilityComp->GetAttributeRawValue(HealthTag), 100.f);

			TagGrantor->RemoveFromRoot(); Blocked->RemoveFromRoot();
		});

		It("allows the effect when ApplicationMustHaveTags are satisfied", [this]()
		{
			UGMCAbilityEffect* TagGrantor = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Allowed    = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			TagGrantor->AddToRoot(); Allowed->AddToRoot();

			// Grant Burning first.
			FGMCAbilityEffectData GrantData;
			GrantData.EffectType = EGMASEffectType::Persistent;
			GrantData.Duration   = 0.f;
			GrantData.GrantedTags.AddTag(BurningTag);
			AbilityComp->ApplyAbilityEffect(TagGrantor, GrantData);
			AbilityComp->GenPredictionTick(1.f);

			// Bonus damage requires Burning — should succeed.
			FGMCAbilityEffectData BonusData;
			BonusData.EffectType = EGMASEffectType::Instant;
			BonusData.ApplicationMustHaveTags.AddTag(BurningTag);
			BonusData.Modifiers.Add(MakeHealthMod(-30.f));
			AbilityComp->ApplyAbilityEffect(Allowed, BonusData);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("Damage applied when ApplicationMustHaveTags are met",
				AbilityComp->GetAttributeRawValue(HealthTag), 70.f);

			TagGrantor->RemoveFromRoot(); Allowed->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
