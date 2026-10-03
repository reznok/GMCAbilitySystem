// Layer 2 tests: effect stacking, multi-attribute modifiers, and concurrent effect stress.
//
// Complements GMAS_EffectSpec (single-effect paths) by exercising scenarios that
// involve multiple simultaneous effects interacting on the same attribute or
// across multiple attributes.
//
// Harness wires Health [0,200]=100 and Mana [0,100]=50 to a fresh ability
// component.  Two helper methods (MakeMod / MakePersistentData) minimise test
// boilerplate.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "UGMAS_TestMovementCmp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASEffectStackingSpec,
	"GMAS.Unit.EffectStacking",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;

	FGameplayTag HealthTag;
	FGameplayTag ManaTag;

	void SetupHarness();
	void TeardownHarness();

	// Build a modifier targeting the given attribute tag with an additive amount.
	FGMCAttributeModifier MakeMod(FGameplayTag Tag, float Amount) const;

	// Build a ready-to-use persistent (reversible) FGMCAbilityEffectData with one modifier.
	FGMCAbilityEffectData MakePersistentData(FGameplayTag Tag, float Amount) const;

END_DEFINE_SPEC(FGMASEffectStackingSpec)

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

void FGMASEffectStackingSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Health"), TEXT("Health for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SManaTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Mana"), TEXT("Mana for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag = SHealthTag.GetTag();
	ManaTag   = SManaTag.GetTag();

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

	FAttributeData ManaData;
	ManaData.AttributeTag = ManaTag;
	ManaData.DefaultValue = 50.f;
	ManaData.Clamp.Min    = 0.f;
	ManaData.Clamp.Max    = 100.f;
	ManaData.bGMCBound    = true;
	AttrData->AttributeData.Add(ManaData);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = -1.0;
}

void FGMASEffectStackingSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASEffectStackingSpec::MakeMod(FGameplayTag Tag, float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = Tag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

FGMCAbilityEffectData FGMASEffectStackingSpec::MakePersistentData(FGameplayTag Tag, float Amount) const
{
	FGMCAbilityEffectData D;
	D.EffectType        = EGMASEffectType::Persistent;
	D.bNegateEffectAtEnd = true;
	D.Duration          = 0.f; // infinite at Layer 2
	D.Modifiers.Add(MakeMod(Tag, Amount));
	return D;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASEffectStackingSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── Multi-attribute modifier ──────────────────────────────────────────────
	// A single effect can carry modifiers for multiple attributes.  Both
	// attributes must be updated in the same tick.
	Describe("Multi-attribute modifier", [this]()
	{
		It("one effect with two modifiers updates both attributes in one tick", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType        = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration          = 0.f;
			Data.Modifiers.Add(MakeMod(HealthTag, 30.f));  // Health 100 → 130
			Data.Modifiers.Add(MakeMod(ManaTag,   20.f));  // Mana   50 → 70

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("Health buffed to 130", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
			TestEqual("Mana buffed to 70",    AbilityComp->GetAttributeValueByTag(ManaTag),    70.f);

			Effect->RemoveFromRoot();
		});

		It("removing the multi-attribute effect reverts both attributes", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType        = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.Duration          = 0.f;
			Data.Modifiers.Add(MakeMod(HealthTag, 30.f));
			Data.Modifiers.Add(MakeMod(ManaTag,   20.f));

			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->GenPredictionTick(1.f);

			AbilityComp->RemoveActiveAbilityEffect(Effect);
			AbilityComp->GenPredictionTick(0.f);

			TestEqual("Health reverts to 100", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestEqual("Mana reverts to 50",    AbilityComp->GetAttributeValueByTag(ManaTag),    50.f);

			Effect->RemoveFromRoot();
		});
	});

	// ── Persistent buff stacked over instant damage ───────────────────────────
	// An instant effect permanently modifies RawValue; a subsequent persistent
	// buff is calculated on top of that new baseline, then reverts cleanly.
	Describe("Persistent over instant interaction", [this]()
	{
		It("persistent buff stacks correctly on top of instant damage (RawValue changed)", [this]()
		{
			// Instant -50: RawValue = 50, Value = 50
			UGMCAbilityEffect* Damage = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Damage->AddToRoot();
			FGMCAbilityEffectData DmgData;
			DmgData.EffectType = EGMASEffectType::Instant;
			DmgData.Modifiers.Add(MakeMod(HealthTag, -50.f));
			AbilityComp->ApplyAbilityEffect(Damage, DmgData);
			AbilityComp->GenPredictionTick(1.f);
			TestEqual("RawValue = 50 after instant damage", AbilityComp->GetAttributeRawValue(HealthTag), 50.f);

			// Persistent +30: RawValue still 50, Value = 80
			UGMCAbilityEffect* Buff = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Buff->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Buff, MakePersistentData(HealthTag, 30.f));
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("RawValue unchanged by persistent buff", AbilityComp->GetAttributeRawValue(HealthTag), 50.f);
			TestEqual("Value = 80 with persistent buff",       AbilityComp->GetAttributeValueByTag(HealthTag), 80.f);

			Damage->RemoveFromRoot(); Buff->RemoveFromRoot();
		});

		It("removing persistent buff exposes the instant-damaged RawValue", [this]()
		{
			UGMCAbilityEffect* Damage = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Buff   = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Damage->AddToRoot(); Buff->AddToRoot();

			FGMCAbilityEffectData DmgData;
			DmgData.EffectType = EGMASEffectType::Instant;
			DmgData.Modifiers.Add(MakeMod(HealthTag, -50.f));
			AbilityComp->ApplyAbilityEffect(Damage, DmgData);
			AbilityComp->ApplyAbilityEffect(Buff, MakePersistentData(HealthTag, 30.f));
			AbilityComp->GenPredictionTick(1.f);
			TestEqual("Value = 80 with buff active", AbilityComp->GetAttributeValueByTag(HealthTag), 80.f);

			AbilityComp->RemoveActiveAbilityEffect(Buff);
			AbilityComp->GenPredictionTick(0.f);
			TestEqual("Value reverts to RawValue=50 after buff removed",
				AbilityComp->GetAttributeValueByTag(HealthTag), 50.f);

			Damage->RemoveFromRoot(); Buff->RemoveFromRoot();
		});
	});

	// ── Selective persistent removal ─────────────────────────────────────────
	// Three concurrent persistent buffs are applied.  Removing individual effects
	// must recalculate the remaining modifiers correctly.
	Describe("Selective persistent removal", [this]()
	{
		It("removing one of three stacked buffs recalculates the remaining sum", [this]()
		{
			UGMCAbilityEffect* B1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* B2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* B3 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			B1->AddToRoot(); B2->AddToRoot(); B3->AddToRoot();

			AbilityComp->ApplyAbilityEffect(B1, MakePersistentData(HealthTag, 10.f));
			AbilityComp->ApplyAbilityEffect(B2, MakePersistentData(HealthTag, 20.f));
			AbilityComp->ApplyAbilityEffect(B3, MakePersistentData(HealthTag, 30.f));
			AbilityComp->GenPredictionTick(1.f);
			TestEqual("100 + 10 + 20 + 30 = 160 with all three active",
				AbilityComp->GetAttributeValueByTag(HealthTag), 160.f);

			// Remove the largest buff (+30).
			AbilityComp->RemoveActiveAbilityEffect(B3);
			AbilityComp->GenPredictionTick(0.f);
			TestEqual("160 - 30 = 130 after B3 removed",
				AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);

			// Remove the middle buff (+20).
			AbilityComp->RemoveActiveAbilityEffect(B2);
			AbilityComp->GenPredictionTick(0.f);
			TestEqual("130 - 20 = 110 after B2 removed",
				AbilityComp->GetAttributeValueByTag(HealthTag), 110.f);

			// Remove the last buff (+10).
			AbilityComp->RemoveActiveAbilityEffect(B1);
			AbilityComp->GenPredictionTick(0.f);
			TestEqual("110 - 10 = 100 after all removed",
				AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			B1->RemoveFromRoot(); B2->RemoveFromRoot(); B3->RemoveFromRoot();
		});
	});

	// ── Stress: many concurrent effects ──────────────────────────────────────
	// Verify correctness and clamping under a high number of simultaneous effects.
	Describe("Stress: many concurrent effects", [this]()
	{
		It("50 instant +2 effects compound and clamp Health at Max=200", [this]()
		{
			// 100 + 50*2 = 200 — exactly at Max, no overflow.
			TArray<UGMCAbilityEffect*> Effects;
			Effects.Reserve(50);
			for (int32 i = 0; i < 50; ++i)
			{
				UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage());
				E->AddToRoot();
				Effects.Add(E);
				FGMCAbilityEffectData D;
				D.EffectType = EGMASEffectType::Instant;
				D.Modifiers.Add(MakeMod(HealthTag, 2.f));
				AbilityComp->ApplyAbilityEffect(E, D);
			}
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("Health == 200 after 50 × +2 instant effects",
				AbilityComp->GetAttributeValueByTag(HealthTag), 200.f);
			TestTrue("All instant effects consumed", AbilityComp->GetActiveEffects().IsEmpty());

			for (UGMCAbilityEffect* E : Effects) { E->RemoveFromRoot(); }
		});

		It("20 persistent +6 buffs clamp Health at Max=200; all removed reverts to RawValue", [this]()
		{
			// 100 + 20*6 = 220, clamped to 200.
			TArray<UGMCAbilityEffect*> Effects;
			Effects.Reserve(20);
			for (int32 i = 0; i < 20; ++i)
			{
				UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage());
				E->AddToRoot();
				Effects.Add(E);
				AbilityComp->ApplyAbilityEffect(E, MakePersistentData(HealthTag, 6.f));
			}
			AbilityComp->GenPredictionTick(1.f);

			TestEqual("Health clamped to 200", AbilityComp->GetAttributeValueByTag(HealthTag), 200.f);
			TestEqual("RawValue unchanged at 100", AbilityComp->GetAttributeRawValue(HealthTag), 100.f);

			// Remove all persistent effects.
			for (UGMCAbilityEffect* E : Effects)
			{
				AbilityComp->RemoveActiveAbilityEffect(E);
			}
			AbilityComp->GenPredictionTick(0.f);

			TestEqual("Health reverts to 100 after all buffs removed",
				AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			for (UGMCAbilityEffect* E : Effects) { E->RemoveFromRoot(); }
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
