// Unit + integration tests for FGMCAttributeModifier::ResolveConditions — tag-driven
// conditional modifiers (Skip / OverrideValue). Headless harness (UGMAS_TestMovementCmp +
// UGMC_AbilitySystemComponent), no world, no network stack.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestMovementCmp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASModifierConditionsSpec,
	"GMAS.Unit.ModifierConditions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;
	UGMCAttributesData*          AttrData    = nullptr;

	FGameplayTag HealthTag;
	FGameplayTag StressTag;
	FGameplayTag EncumberedTag;
	FGameplayTag BleedTag;

	void SetupHarness();
	void TeardownHarness();

	// "match any of these tags" query over a single tag.
	static FGameplayTagQuery AnyOf(FGameplayTag Tag);
	// A modifier targeting Health, Add op, raw value source.
	FGMCAttributeModifier MakeHealthMod(float Amount) const;
	// Integration helper: apply an Instant Health modifier carrying the given rules.
	void ApplyInstantHealth(float Amount, const TArray<FGMCModifierCondition>& Rules);

END_DEFINE_SPEC(FGMASModifierConditionsSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASModifierConditionsSpec::SetupHarness()
{
	static FNativeGameplayTag SHealth(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Cond.Attribute.Health"), TEXT("Health for condition tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SStress(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Cond.Attribute.Stress"), TEXT("Stress for condition tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SEnc(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Cond.Status.Encumbered"), TEXT("Encumbered status"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBleed(TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Cond.Status.Bleed"), TEXT("Bleed status"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag     = SHealth.GetTag();
	StressTag     = SStress.GetTag();
	EncumberedTag = SEnc.GetTag();
	BleedTag      = SBleed.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();
	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	auto AddAttr = [&](FGameplayTag Tag, float Default, float Min, float Max)
	{
		FAttributeData D;
		D.AttributeTag = Tag;
		D.DefaultValue = Default;
		D.Clamp.Min    = Min;
		D.Clamp.Max    = Max;
		D.bGMCBound    = true;
		AttrData->AttributeData.Add(D);
	};
	AddAttr(HealthTag, 100.f, 0.f, 200.f);
	AddAttr(StressTag, 40.f,  0.f, 100.f);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = 1.0;
}

void FGMASModifierConditionsSpec::TeardownHarness()
{
	if (AttrData)    { AttrData->RemoveFromRoot();    AttrData = nullptr; }
	if (AbilityComp) { AbilityComp->RemoveFromRoot(); AbilityComp = nullptr; }
	if (MoveCmp)     { MoveCmp->RemoveFromRoot();     MoveCmp = nullptr; }
}

FGameplayTagQuery FGMASModifierConditionsSpec::AnyOf(FGameplayTag Tag)
{
	FGameplayTagContainer C;
	C.AddTag(Tag);
	return FGameplayTagQuery::MakeQuery_MatchAnyTags(C);
}

FGMCAttributeModifier FGMASModifierConditionsSpec::MakeHealthMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	Mod.DeltaTime     = 1.f;
	return Mod;
}

void FGMASModifierConditionsSpec::ApplyInstantHealth(float Amount, const TArray<FGMCModifierCondition>& Rules)
{
	UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
	Effect->AddToRoot();

	FGMCAbilityEffectData Data;
	Data.EffectType  = EGMASEffectType::Instant;
	Data.bServerAuth = true;
	FGMCAttributeModifier Mod = MakeHealthMod(Amount);
	Mod.Conditions = Rules;
	Data.Modifiers.Add(Mod);

	AbilityComp->ApplyAbilityEffect(Effect, Data);
	AbilityComp->ProcessAttributes(true); // GMC-bound path (bGMCBound=true)

	Effect->RemoveFromRoot();
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASModifierConditionsSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("No / empty conditions", [this]()
	{
		It("applies (returns true) when there are no rules", [this]()
		{
			FGMCAttributeModifier Mod = MakeHealthMod(10.f);
			TestTrue ("no rules -> apply", Mod.ResolveConditions(AbilityComp));
			TestEqual("value untouched", Mod.ModifierValue, 10.f);
		});

		It("ignores a rule with an empty query", [this]()
		{
			FGMCAttributeModifier Mod = MakeHealthMod(10.f);
			FGMCModifierCondition Rule; // empty Condition, default Action=Skip
			Mod.Conditions.Add(Rule);
			TestTrue("empty query never matches -> apply", Mod.ResolveConditions(AbilityComp));
		});

		It("returns true when ASC is null (no crash, applies)", [this]()
		{
			FGMCAttributeModifier Mod = MakeHealthMod(10.f);
			FGMCModifierCondition Rule;
			Rule.Condition = AnyOf(BleedTag);
			Rule.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Rule);
			TestTrue("null ASC -> apply", Mod.ResolveConditions(nullptr));
		});
	});

	Describe("Skip action", [this]()
	{
		It("skips (returns false) when the tag is present", [this]()
		{
			AbilityComp->AddActiveTag(BleedTag);
			FGMCAttributeModifier Mod = MakeHealthMod(5.f);
			FGMCModifierCondition Rule;
			Rule.Condition = AnyOf(BleedTag);
			Rule.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Rule);
			TestFalse("bleed present -> skip", Mod.ResolveConditions(AbilityComp));
		});

		It("applies when the tag is absent", [this]()
		{
			FGMCAttributeModifier Mod = MakeHealthMod(5.f);
			FGMCModifierCondition Rule;
			Rule.Condition = AnyOf(BleedTag);
			Rule.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Rule);
			TestTrue("no bleed -> apply", Mod.ResolveConditions(AbilityComp));
		});
	});

	Describe("OverrideValue action", [this]()
	{
		It("overrides the raw value when the tag is present", [this]()
		{
			AbilityComp->AddActiveTag(EncumberedTag);
			FGMCAttributeModifier Mod = MakeHealthMod(-10.f);
			FGMCModifierCondition Rule;
			Rule.Condition     = AnyOf(EncumberedTag);
			Rule.Action        = EGMCModifierConditionAction::OverrideValue;
			Rule.ValueType     = EGMCAttributeModifierType::AMT_Value;
			Rule.ModifierValue = -25.f;
			Mod.Conditions.Add(Rule);

			TestTrue ("encumbered -> apply", Mod.ResolveConditions(AbilityComp));
			TestEqual("value overridden to -25", Mod.ModifierValue, -25.f);
			TestTrue ("still AMT_Value", Mod.ValueType == EGMCAttributeModifierType::AMT_Value);
		});

		It("swaps the value source to an attribute when the tag is present", [this]()
		{
			AbilityComp->AddActiveTag(EncumberedTag);
			FGMCAttributeModifier Mod = MakeHealthMod(10.f);
			FGMCModifierCondition Rule;
			Rule.Condition        = AnyOf(EncumberedTag);
			Rule.Action           = EGMCModifierConditionAction::OverrideValue;
			Rule.ValueType        = EGMCAttributeModifierType::AMT_Attribute;
			Rule.ValueAsAttribute = StressTag;
			Mod.Conditions.Add(Rule);

			TestTrue("encumbered -> apply", Mod.ResolveConditions(AbilityComp));
			TestTrue("ValueType swapped to AMT_Attribute", Mod.ValueType == EGMCAttributeModifierType::AMT_Attribute);
			TestTrue("ValueAsAttribute set to Stress", Mod.ValueAsAttribute == StressTag);
		});
	});

	Describe("Ordering & replay-safety", [this]()
	{
		It("first matching rule wins", [this]()
		{
			AbilityComp->AddActiveTag(EncumberedTag);
			AbilityComp->AddActiveTag(BleedTag);
			FGMCAttributeModifier Mod = MakeHealthMod(10.f);

			FGMCModifierCondition Override; // rule [0]
			Override.Condition     = AnyOf(EncumberedTag);
			Override.Action        = EGMCModifierConditionAction::OverrideValue;
			Override.ValueType     = EGMCAttributeModifierType::AMT_Value;
			Override.ModifierValue = 99.f;
			FGMCModifierCondition Skip;     // rule [1]
			Skip.Condition = AnyOf(BleedTag);
			Skip.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Override);
			Mod.Conditions.Add(Skip);

			TestTrue ("override (rule 0) wins over skip (rule 1)", Mod.ResolveConditions(AbilityComp));
			TestEqual("overridden value applied", Mod.ModifierValue, 99.f);
		});

		It("ignores client-auth-only tags (bound view only)", [this]()
		{
			// Tag posed via the NON-bound client-auth container -> not in GetBoundActiveTags().
			AbilityComp->AddClientAuthActiveTag(BleedTag);
			FGMCAttributeModifier Mod = MakeHealthMod(5.f);
			FGMCModifierCondition Rule;
			Rule.Condition = AnyOf(BleedTag);
			Rule.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Rule);

			TestTrue("client-auth tag must NOT trigger the condition", Mod.ResolveConditions(AbilityComp));
		});
	});

	Describe("Integration — Instant effect application", [this]()
	{
		It("skip rule with tag present leaves the attribute unchanged", [this]()
		{
			AbilityComp->AddActiveTag(BleedTag);
			FGMCModifierCondition Skip;
			Skip.Condition = AnyOf(BleedTag);
			Skip.Action    = EGMCModifierConditionAction::Skip;
			ApplyInstantHealth(30.f, { Skip });
			TestEqual("health unchanged (skipped)", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
		});

		It("skip rule with tag absent applies normally", [this]()
		{
			FGMCModifierCondition Skip;
			Skip.Condition = AnyOf(BleedTag);
			Skip.Action    = EGMCModifierConditionAction::Skip;
			ApplyInstantHealth(30.f, { Skip });
			TestEqual("health buffed (not skipped)", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
		});

		It("override value rule applies the overridden amount", [this]()
		{
			AbilityComp->AddActiveTag(EncumberedTag);
			FGMCModifierCondition Over;
			Over.Condition     = AnyOf(EncumberedTag);
			Over.Action        = EGMCModifierConditionAction::OverrideValue;
			Over.ValueType     = EGMCAttributeModifierType::AMT_Value;
			Over.ModifierValue = 5.f;
			ApplyInstantHealth(30.f, { Over });
			TestEqual("health +5 (overridden), not +30", AbilityComp->GetAttributeValueByTag(HealthTag), 105.f);
		});
	});

	Describe("Persistent re-evaluation (constant buff toggle)", [this]()
	{
		It("toggles a constant buff off and on with the tag when bReevaluate is set", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;                    // -> temporal (removable) modifiers
			Data.bReevaluateConditionsWhilePersistent = true;
			Data.Duration = 1000.0;                            // effectively infinite for the test
			Data.bServerAuth = true;

			FGMCAttributeModifier Mod = MakeHealthMod(50.f);
			FGMCModifierCondition Skip;
			Skip.Condition = AnyOf(BleedTag);
			Skip.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Skip);
			Data.Modifiers.Add(Mod);

			AbilityComp->ActionTimer = 1.0;
			AbilityComp->ApplyAbilityEffect(Effect, Data);     // StartEffect applies +50 (no bleed)
			AbilityComp->ProcessAttributes(true);
			TestEqual("buff active at start (no bleed)", AbilityComp->GetAttributeValueByTag(HealthTag), 150.f);

			AbilityComp->AddActiveTag(BleedTag);               // tag appears -> next tick suppresses
			AbilityComp->ActionTimer = 2.0;
			AbilityComp->TickActiveEffects(1.0f);
			AbilityComp->ProcessAttributes(true);
			TestEqual("buff suppressed while bleeding", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			AbilityComp->RemoveActiveTag(BleedTag);            // tag gone -> next tick restores
			AbilityComp->ActionTimer = 3.0;
			AbilityComp->TickActiveEffects(1.0f);
			AbilityComp->ProcessAttributes(true);
			TestEqual("buff restored after bleed ends", AbilityComp->GetAttributeValueByTag(HealthTag), 150.f);

			Effect->RemoveFromRoot();
		});

		It("does NOT toggle when bReevaluate is false (gate at start only)", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.bNegateEffectAtEnd = true;
			Data.bReevaluateConditionsWhilePersistent = false; // the switch is OFF
			Data.Duration = 1000.0;
			Data.bServerAuth = true;

			FGMCAttributeModifier Mod = MakeHealthMod(50.f);
			FGMCModifierCondition Skip;
			Skip.Condition = AnyOf(BleedTag);
			Skip.Action    = EGMCModifierConditionAction::Skip;
			Mod.Conditions.Add(Skip);
			Data.Modifiers.Add(Mod);

			AbilityComp->ActionTimer = 1.0;
			AbilityComp->ApplyAbilityEffect(Effect, Data);     // applied at start (no bleed) -> +50
			AbilityComp->ProcessAttributes(true);
			TestEqual("buff active at start", AbilityComp->GetAttributeValueByTag(HealthTag), 150.f);

			AbilityComp->AddActiveTag(BleedTag);               // tag appears AFTER start
			AbilityComp->ActionTimer = 2.0;
			AbilityComp->TickActiveEffects(1.0f);
			AbilityComp->ProcessAttributes(true);
			TestEqual("buff stays — no re-evaluation", AbilityComp->GetAttributeValueByTag(HealthTag), 150.f);

			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
