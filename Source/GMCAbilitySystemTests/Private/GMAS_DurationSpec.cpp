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
	Data.bServerAuth       = true;  // skip client prediction timeout — duration tests need large timer jumps
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
			// Duration=5, StartT=1 → EndTime=6.  Check at t=4: still active.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 5.f, 30.f, 1.0, 4.0);
			TestEqual("Value buffed while active", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
			TestFalse("Effect still in ActiveEffects", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("effect is removed and value reverts at exactly EndTime", [this]()
		{
			// Duration=5, StartT=1 → EndTime=6.  At t=6 the check is >=, so it expires.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 5.f, 30.f, 1.0, 6.0);
			TestEqual("Value reverted to 100 at EndTime", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestTrue("Effect removed from ActiveEffects", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("effect is removed after EndTime is exceeded", [this]()
		{
			// Duration=2, StartT=1 → EndTime=3.  Check at t=11: long expired.
			UGMCAbilityEffect* Effect = ApplyAndTick(EGMASEffectType::Persistent, 2.f, 50.f, 1.0, 11.0);
			TestEqual("Value back to 100 after expiry", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestTrue("Effect removed", AbilityComp->GetActiveEffects().IsEmpty());
			Effect->RemoveFromRoot();
		});

		It("two effects with different durations expire independently", [this]()
		{
			// Effect A: Duration=3, +20.  Effect B: Duration=7, +15.  StartT=1.
			// EndTime A=4, EndTime B=8.  Check at t=5: A expired (5>=4), B active (5<8).
			UGMCAbilityEffect* EffA = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* EffB = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EffA->AddToRoot(); EffB->AddToRoot();

			AbilityComp->ActionTimer = 1.0;

			auto MakeData = [&](float Dur, float Mod) {
				FGMCAbilityEffectData D;
				D.EffectType        = EGMASEffectType::Persistent;
				D.bNegateEffectAtEnd = true;
				D.Duration          = Dur;
				D.bServerAuth       = true;
				D.Modifiers.Add(MakeHealthMod(Mod));
				return D;
			};

			AbilityComp->ApplyAbilityEffect(EffA, MakeData(3.f, 20.f));
			AbilityComp->ApplyAbilityEffect(EffB, MakeData(7.f, 15.f));

			AbilityComp->ActionTimer = 5.0;
			AbilityComp->TickActiveEffects(4.f);
			AbilityComp->ProcessAttributes(true);

			// A expired (EndTime=4 <= t=5), B still live (EndTime=8 > t=5).
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
			// Interval=1s, +10 per tick.  Start at t=1.
			// Simulate 7 half-second steps (t=1.5 … t=4.5).
			// Periodic fires when TruncToInt(elapsed/1) crosses an integer boundary:
			//   t=2.0 (elapsed=1.0): period 0→1 → 1 tick
			//   t=3.0 (elapsed=2.0): period 1→2 → 1 tick
			//   t=4.0 (elapsed=3.0): period 2→3 → 1 tick  → total 3 ticks
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 1.0;

			FGMCAbilityEffectData Data;
			Data.EffectType          = EGMASEffectType::Periodic;
			Data.Duration            = 0.f;  // infinite
			Data.PeriodicInterval    = 1.f;
			Data.bPeriodicFirstTick  = false;
			Data.bServerAuth         = true;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);

			// Simulate 7 frames of 0.5 s each (t=1.5 … t=4.5).
			constexpr float Step = 0.5f;
			for (int i = 1; i <= 7; i++)
			{
				AbilityComp->ActionTimer = 1.0 + i * Step;
				AbilityComp->TickActiveEffects(Step);
			}
			AbilityComp->ProcessAttributes(true);

			// 3 ticks × 10 = +30 permanently on RawValue.
			TestNearlyEqual("RawValue = 130 after 3 periodic ticks",
				AbilityComp->GetAttributeRawValue(HealthTag), 130.f, KINDA_SMALL_NUMBER);

			Effect->RemoveFromRoot();
		});

		It("fires the first tick immediately when bPeriodicFirstTick is true", [this]()
		{
			// bPeriodicFirstTick=true → first tick fires on apply at t=1 (+10).
			// Then simulate 3 half-second steps (t=1.5 … t=2.5).
			// Periodic fires again at t=2.0 (elapsed 0→1): total 2 ticks → +20.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 1.0;

			FGMCAbilityEffectData Data;
			Data.EffectType          = EGMASEffectType::Periodic;
			Data.Duration            = 0.f;
			Data.PeriodicInterval    = 1.f;
			Data.bPeriodicFirstTick  = true;
			Data.bServerAuth         = true;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data); // first tick fires here

			// Simulate 3 frames of 0.5 s each (t=1.5 … t=2.5).
			constexpr float Step = 0.5f;
			for (int i = 1; i <= 3; i++)
			{
				AbilityComp->ActionTimer = 1.0 + i * Step;
				AbilityComp->TickActiveEffects(Step);
			}
			AbilityComp->ProcessAttributes(true);

			// First-tick at apply + one interval tick at t=2 = 2 ticks × 10 = +20.
			TestNearlyEqual("RawValue = 120 (first-tick + one interval)",
				AbilityComp->GetAttributeRawValue(HealthTag), 120.f, KINDA_SMALL_NUMBER);

			Effect->RemoveFromRoot();
		});

		It("periodic effect with finite duration stops firing after expiry", [this]()
		{
			// StartT=1, Interval=1s, Duration=2.5s → EndTime=3.5.
			// Simulate 5 half-second steps (t=1.5 … t=3.5).
			// Periodic fires at t=2 (period 0→1) and t=3 (period 1→2) → 2 ticks.
			// At t=3.5 ActionTimer >= EndTime → effect expires with 0 new ticks.
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			AbilityComp->ActionTimer = 1.0;

			FGMCAbilityEffectData Data;
			Data.EffectType          = EGMASEffectType::Periodic;
			Data.Duration            = 2.5f;
			Data.PeriodicInterval    = 1.f;
			Data.bPeriodicFirstTick  = false;
			Data.bServerAuth         = true;
			Data.Modifiers.Add(MakeHealthMod(10.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);

			// Simulate 5 frames of 0.5 s each (t=1.5 … t=3.5).
			constexpr float Step = 0.5f;
			for (int i = 1; i <= 5; i++)
			{
				AbilityComp->ActionTimer = 1.0 + i * Step;
				AbilityComp->TickActiveEffects(Step);
			}
			AbilityComp->ProcessAttributes(true);

			// 2 ticks × 10 = +20 permanently; effect expired at EndTime=3.5 and removed.
			TestNearlyEqual("RawValue = 120 (2 ticks before expiry)",
				AbilityComp->GetAttributeRawValue(HealthTag), 120.f, KINDA_SMALL_NUMBER);
			TestTrue("Effect removed after duration expiry",
				AbilityComp->GetActiveEffects().IsEmpty());

			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
