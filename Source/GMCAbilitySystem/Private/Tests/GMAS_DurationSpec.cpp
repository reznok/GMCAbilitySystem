// Layer 2 tests: duration-based effect expiry and periodic effects.
//
// These tests require ActionTimer to advance beyond an effect's EndTime.
// GenPredictionTick cannot be used here because it overwrites ActionTimer from
// GMCMovementComponent->GetMoveTimestamp() (which always returns -1.0 on the
// headless stub).  Instead, ActionTimer is set directly and TickActiveEffects +
// ProcessAttributes are called explicitly — the same sub-operations that
// GenPredictionTick performs, but with controllable timing.
//
// Layer 3 note: duration and periodic tests do not require a network connection;
// they require controlled ActionTimer advancement, which is a GMC-tick coupling
// issue rather than a networking issue.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "UGMAS_TestMovementCmp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASDurationSpec,
	"GMAS.Unit.Duration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;
	FGameplayTag HealthTag;

	void SetupHarness();
	void TeardownHarness();
	FGMCAttributeModifier MakeHealthMod(float Amount) const;

	// Apply effect, start it at ActionTimer=StartT, then advance to CheckT and tick.
	// Returns the applied effect pointer.
	UGMCAbilityEffect* ApplyAndTick(EGMASEffectType Type, float Duration,
	                                float ModAmount, double StartT, double CheckT);

END_DEFINE_SPEC(FGMASDurationSpec)

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

void FGMASDurationSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Health"), TEXT("Health for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag = SHealthTag.GetTag();

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
	HealthData.Clamp.Max    = 500.f;
	HealthData.bGMCBound    = true;
	AttrData->AttributeData.Add(HealthData);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	// Start at t=0 so effects can use positive timestamps.
	AbilityComp->ActionTimer = 0.0;
}

void FGMASDurationSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASDurationSpec::MakeHealthMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

UGMCAbilityEffect* FGMASDurationSpec::ApplyAndTick(EGMASEffectType Type, float Duration,
                                                    float ModAmount, double StartT, double CheckT)
{
	UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
	Effect->AddToRoot();

	AbilityComp->ActionTimer = StartT;

	FGMCAbilityEffectData Data;
	Data.EffectType        = Type;
	Data.bNegateEffectAtEnd = (Type == EGMASEffectType::Persistent);
	Data.Duration          = Duration;
	Data.Modifiers.Add(MakeHealthMod(ModAmount));

	AbilityComp->ApplyAbilityEffect(Effect, Data);

	// Advance to CheckT and tick directly — bypasses GenPredictionTick's
	// ActionTimer overwrite from GetMoveTimestamp().
	AbilityComp->ActionTimer = CheckT;
	AbilityComp->TickActiveEffects(static_cast<float>(CheckT - StartT));
	AbilityComp->ProcessAttributes(true);  // GMC-bound path (bGMCBound=true)

	return Effect; // caller is responsible for RemoveFromRoot()
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASDurationSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── Duration-based persistent effect expiry ────────────────────────────
	Describe("Duration-based persistent effect expiry", [this]()
	{
		It("effect is active before EndTime is reached", [this]()
		{
			// Duration=5 → EndTime=5.  Check at t=3: still active.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 5.f, 30.f, 0.0, 3.0);
			TestEqual("Value buffed while active", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
			TestFalse("Effect still in ActiveEffects", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("effect is removed and value reverts at exactly EndTime", [this]()
		{
			// Duration=5 → EndTime=5.  At t=5 the check is >=, so it expires.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 5.f, 30.f, 0.0, 5.0);
			TestEqual("Value reverted to 100 at EndTime", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestTrue("Effect removed from ActiveEffects", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("effect is removed after EndTime is exceeded", [this]()
		{
			// Duration=2 → EndTime=2.  Check at t=10: long expired.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 2.f, 50.f, 0.0, 10.0);
			TestEqual("Value back to 100 after expiry", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestTrue("Effect removed", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("two effects with different durations expire independently", [this]()
		{
			// Effect A: Duration=3, +20.  Effect B: Duration=7, +15.
			// At t=4: A expired (EndTime=3), B still active (EndTime=7).
			UGMCAbilityEffect* EffA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffA->AddToRoot(); EffB->AddToRoot();

			AbilityComp->ActionTimer = 0.0;

			auto MakeData = [&](float Dur, float Mod) {
				FGMCAbilityEffectData D;
				D.EffectType        = EGMASEffectType::Persistent;
				D.bNegateEffectAtEnd = true;
				D.Duration          = Dur;
				D.Modifiers.Add(MakeHealthMod(Mod));
				return D;
			};

			AbilityComp->ApplyAbilityEffect(EffA, MakeData(3.f, 20.f));
			AbilityComp->ApplyAbilityEffect(EffB, MakeData(7.f, 15.f));

			AbilityComp->ActionTimer = 4.0;
			AbilityComp->TickActiveEffects(4.f);
			AbilityComp->ProcessAttributes(true);

			// A expired (EndTime=3 <= t=4), B still live (EndTime=7 > t=4).
			TestEqual("100 + 15 = 115 (only B active)", AbilityComp->GetAttributeValueByTag(HealthTag), 115.f);

			EffA->RemoveFromRoot(); EffB->RemoveFromRoot();
		});
	});

	// ── Periodic effects ───────────────────────────────────────────────────
	// Periodic effects fire once per PeriodicInterval, applying the full modifier
	// value each time (not scaled by DeltaTime).  They permanently modify RawValue.
	Describe("Periodic effects", [this]()
	{
		It("fires exactly once per interval over a given timespan", [this]()
		{
			// Interval=1s, +10 per tick.  Run from t=0 to t=3.5 → 3 complete periods.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 0.0;

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Periodic;
			Data.Duration         = 0.f;  // infinite
			Data.PeriodicInterval = 1.f;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);

			// Advance to t=3.5 — three intervals have completed (t=1, t=2, t=3).
			AbilityComp->ActionTimer = 3.5;
			AbilityComp->TickActiveEffects(3.5f);
			AbilityComp->ProcessAttributes(true);

			// 3 ticks × 10 = +30 permanently on RawValue.
			TestNearlyEqual("RawValue = 130 after 3 periodic ticks",
				AbilityComp->GetAttributeRawValue(HealthTag), 130.f, KINDA_SMALL_NUMBER);

			Effect->RemoveFromRoot();
		});

		It("fires the first tick immediately when bPeriodicFirstTick is true", [this]()
		{
			// bPeriodicFirstTick=true → fires at t=0 (on StartEffect) plus t=1.
			// Run from t=0 to t=1.5 → 2 ticks total.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 0.0;

			FGMCAbilityEffectData Data;
			Data.EffectType          = EGMASEffectType::Periodic;
			Data.Duration            = 0.f;
			Data.PeriodicInterval    = 1.f;
			Data.bPeriodicFirstTick  = true;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 1.5;
			AbilityComp->TickActiveEffects(1.5f);
			AbilityComp->ProcessAttributes(true);

			// First-tick at t=0 + second at t=1 = 2 ticks × 10 = +20.
			TestNearlyEqual("RawValue = 120 (first-tick + one interval)",
				AbilityComp->GetAttributeRawValue(HealthTag), 120.f, KINDA_SMALL_NUMBER);

			Effect->RemoveFromRoot();
		});

		It("periodic effect with finite duration stops firing after expiry", [this]()
		{
			// Interval=1s, Duration=2.5s → fires at t=1, t=2; expires at t=2.5.
			// Check at t=10: only 2 ticks should have fired.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 0.0;

			FGMCAbilityEffectData Data;
			Data.EffectType       = EGMASEffectType::Periodic;
			Data.Duration         = 2.5f;
			Data.PeriodicInterval = 1.f;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);

			AbilityComp->ActionTimer = 10.0;
			AbilityComp->TickActiveEffects(10.f);
			AbilityComp->ProcessAttributes(true);

			// 2 ticks × 10 = +20 permanently; effect expired and removed.
			TestNearlyEqual("RawValue = 120 (2 ticks before expiry)",
				AbilityComp->GetAttributeRawValue(HealthTag), 120.f, KINDA_SMALL_NUMBER);
			TestTrue("Effect removed after duration expiry",
				AbilityComp->GetActiveEffects().IsEmpty());

			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
