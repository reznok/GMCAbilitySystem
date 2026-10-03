// Attribute initialization and modifier value-source fixes from the 1.4.1 audit, driven through
// the headless UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent harness (no world, no network
// stack). The harness wires the objects and the shared rows but does NOT bind: each case adds its
// own rows first, then calls BindReplicationData() so InstantiateAttributes sees them.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbilityComponent.h"
#include "GMAS_TestHelpers.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeAuditSpec,
	"GMAS.Unit.AttributeAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;
	UGMCAttributesData*          AttrData    = nullptr;

	FGameplayTag HealthTag;      // bound, [0, 200], default 100
	FGameplayTag MaxTag;         // bound, [0, 500], default 500
	FGameplayTag ManaTag;        // added per case
	FGameplayTag MaxHealthTag;   // hook-ordering cases only
	FGameplayTag MissingTag;     // registered, never declared as a row
	FGameplayTag EffectTag;

	void SetupHarness();
	void TeardownHarness();

	// A component with the test seed hook over its own rows, wired like the harness component.
	// Rooted; the case releases it.
	UGMAS_TestAbilityComponent* MakeHookComponent(UGMCAttributesData* Rows) const;

END_DEFINE_SPEC(FGMASAttributeAuditSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASAttributeAuditSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Attribute.Health"), TEXT("Health for the attribute-audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SMaxTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Attribute.Max"), TEXT("Source attribute for the attribute-audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SManaTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Attribute.Mana"), TEXT("Per-case attribute for the attribute-audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SMaxHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Attribute.MaxHealth"), TEXT("Clamp source for the hook-ordering tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SMissingTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Attribute.Missing"), TEXT("Registered but never declared as an attribute row"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SEffectTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.AttrAudit.Effect.Probe"), TEXT("Effect tag for the attribute-audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag    = SHealthTag.GetTag();
	MaxTag       = SMaxTag.GetTag();
	ManaTag      = SManaTag.GetTag();
	MaxHealthTag = SMaxHealthTag.GetTag();
	MissingTag   = SMissingTag.GetTag();
	EffectTag    = SEffectTag.GetTag();

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
	AddAttr(MaxTag,    500.f, 0.f, 500.f);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->SetActionTimerForTest(GMASTest::StableActionTimer);
	AbilityComp->SilenceEffectIDWrapReportForTest();   // negative clock: ids wrap into their ranges
	// No BindReplicationData() here: each case adds its rows first, then binds.
}

void FGMASAttributeAuditSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

UGMAS_TestAbilityComponent* FGMASAttributeAuditSpec::MakeHookComponent(UGMCAttributesData* Rows) const
{
	UGMAS_TestAbilityComponent* Comp = NewObject<UGMAS_TestAbilityComponent>(GetTransientPackage());
	Comp->AddToRoot();
	Comp->AttributeDataAssets.Add(Rows);   // reachable through the rooted component, so no AddToRoot
	Comp->GMCMovementComponent = MoveCmp;
	Comp->SetActionTimerForTest(GMASTest::StableActionTimer);
	Comp->SilenceEffectIDWrapReportForTest();   // negative clock: ids wrap into their ranges
	return Comp;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASAttributeAuditSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("InstantiateAttributes", [this]()
	{
		It("logs an Error for a row with an active [0,0] clamp", [this]()
		{
			AddExpectedErrorPlain(TEXT("has an active [0, 0] clamp"), EAutomationExpectedErrorFlags::Contains, 1);
			FAttributeData Row; Row.AttributeTag = ManaTag; Row.DefaultValue = 10.f; Row.bGMCBound = true;   // default clamp: both flags on, 0/0
			AttrData->AttributeData.Add(Row);
			AbilityComp->BindReplicationData();
			TestEqual("pinned at 0", AbilityComp->GetAttributeValueByTag(ManaTag), 0.f);
		});

		It("does not log for a row whose bClampMax is off", [this]()
		{
			FAttributeData Row; Row.AttributeTag = ManaTag; Row.DefaultValue = 10.f; Row.bGMCBound = true; Row.Clamp.bClampMax = false;
			AttrData->AttributeData.Add(Row);
			AbilityComp->BindReplicationData();
			TestEqual("10", AbilityComp->GetAttributeValueByTag(ManaTag), 10.f);
		});

		It("logs an Error naming both assets for a duplicate tag and keeps the first row", [this]()
		{
			AddExpectedErrorPlain(TEXT("is declared by both"), EAutomationExpectedErrorFlags::Contains, 1);
			UGMCAttributesData* Second = NewObject<UGMCAttributesData>(GetTransientPackage());   // reachable through the rooted component's AttributeDataAssets, so no AddToRoot
			FAttributeData Dup; Dup.AttributeTag = HealthTag; Dup.DefaultValue = 5.f; Dup.Clamp.Max = 200.f; Dup.bGMCBound = true;
			Second->AttributeData.Add(Dup);
			AbilityComp->AttributeDataAssets.Add(Second);
			AbilityComp->BindReplicationData();
			TestEqual("first declaration wins (100)", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
		});
	});

	Describe("GetAttributeInitialValueByTag", [this]()
	{
		It("returns the applied initial value, not the row", [this]()
		{
			FAttributeData Full; Full.AttributeTag = ManaTag; Full.DefaultValue = 10.f; Full.bStartFull = true; Full.Clamp.Max = 100.f; Full.bGMCBound = true;
			AttrData->AttributeData.Add(Full);
			AbilityComp->BindReplicationData();
			TestEqual("bStartFull resolved to Max", AbilityComp->GetAttributeInitialValueByTag(ManaTag), 100.f);
		});

		It("warns once and returns -1 for an unknown tag", [this]()
		{
			// The warn-once latch (UnknownInitialValueTagsWarned) is per component and keyed by tag; this
			// case's fresh component owns its own.
			AddExpectedErrorPlain(TEXT("is not an attribute of"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->BindReplicationData();
			if (!TestTrue("probe tag is registered", MissingTag.IsValid())) { return; }
			TestEqual("-1", AbilityComp->GetAttributeInitialValueByTag(MissingTag), -1.f);
			TestEqual("-1 again, silent", AbilityComp->GetAttributeInitialValueByTag(MissingTag), -1.f);
		});
	});

	// Bound rows are sorted by tag string, so Health settles before MaxHealth: an override on the
	// dependency must still reach the dependent row (three-pass init).
	Describe("Initial value hook", [this]()
	{
		It("an override on a dependency feeds a bStartFull row clamped by that attribute", [this]()
		{
			UGMCAttributesData* Rows = NewObject<UGMCAttributesData>(GetTransientPackage());
			FAttributeData Health; Health.AttributeTag = HealthTag; Health.bStartFull = true; Health.Clamp.MaxAttributeTag = MaxHealthTag; Health.bGMCBound = true;   // Min 0, Max through MaxHealth
			FAttributeData MaxHealth; MaxHealth.AttributeTag = MaxHealthTag; MaxHealth.DefaultValue = 100.f; MaxHealth.Clamp.Max = 1000.f; MaxHealth.bGMCBound = true;
			Rows->AttributeData.Add(Health);
			Rows->AttributeData.Add(MaxHealth);
			UGMAS_TestAbilityComponent* Comp = MakeHookComponent(Rows);
			Comp->InitialValueOverrides.Add(MaxHealthTag, 250.f);
			Comp->BindReplicationData();
			TestEqual("MaxHealth takes the override", Comp->GetAttributeValueByTag(MaxHealthTag), 250.f);
			TestEqual("Health starts full at the overridden MaxHealth", Comp->GetAttributeValueByTag(HealthTag), 250.f);
			TestEqual("applied initial value of Health", Comp->GetAttributeInitialValueByTag(HealthTag), 250.f);
			Comp->RemoveFromRoot();
		});

		It("an override outside the clamp is clamped with an Error", [this]()
		{
			AddExpectedErrorPlain(TEXT("outside its clamp"), EAutomationExpectedErrorFlags::Contains, 1);
			UGMCAttributesData* Rows = NewObject<UGMCAttributesData>(GetTransientPackage());
			FAttributeData Health; Health.AttributeTag = HealthTag; Health.DefaultValue = 100.f; Health.Clamp.Max = 200.f; Health.bGMCBound = true;
			Rows->AttributeData.Add(Health);
			UGMAS_TestAbilityComponent* Comp = MakeHookComponent(Rows);
			Comp->InitialValueOverrides.Add(HealthTag, 500.f);
			Comp->BindReplicationData();
			TestEqual("clamped to Max", Comp->GetAttributeValueByTag(HealthTag), 200.f);
			TestEqual("applied initial value is the clamped one", Comp->GetAttributeInitialValueByTag(HealthTag), 200.f);
			Comp->RemoveFromRoot();
		});

		It("an override on a bStartFull row wins over bStartFull", [this]()
		{
			UGMCAttributesData* Rows = NewObject<UGMCAttributesData>(GetTransientPackage());
			FAttributeData Health; Health.AttributeTag = HealthTag; Health.bStartFull = true; Health.Clamp.Max = 200.f; Health.bGMCBound = true;
			Rows->AttributeData.Add(Health);
			UGMAS_TestAbilityComponent* Comp = MakeHookComponent(Rows);
			Comp->InitialValueOverrides.Add(HealthTag, 50.f);
			Comp->BindReplicationData();
			TestEqual("hook value, not Max", Comp->GetAttributeValueByTag(HealthTag), 50.f);
			TestEqual("applied initial value", Comp->GetAttributeInitialValueByTag(HealthTag), 50.f);
			Comp->RemoveFromRoot();
		});
	});

	Describe("Modifier value sources", [this]()
	{
		It("AddScaledBetween with attribute-driven bounds lerps between the resolved values", [this]()
		{
			// Regression for the literal-X/Y clamp: with both bounds taken from attributes the literals
			// are 0, and the old clamp squashed every result to 0.
			FAttributeData Row; Row.AttributeTag = ManaTag; Row.DefaultValue = 100.f; Row.Clamp.Max = 1000.f; Row.bGMCBound = true;
			AttrData->AttributeData.Add(Row);
			AbilityComp->BindReplicationData();
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = EffectTag;
			FGMCAttributeModifier Mod; Mod.AttributeTag = ManaTag; Mod.Op = EModifierType::AddScaledBetween;
			Mod.ValueType = EGMCAttributeModifierType::AMT_Value; Mod.ModifierValue = 0.5f;
			Mod.XAsAttribute = true; Mod.XAttribute = MaxTag;    // 500
			Mod.YAsAttribute = true; Mod.YAttribute = ManaTag;   // 100
			Data.Modifiers.Add(Mod);
			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ProcessAttributes(true);
			TestEqual("100 + lerp(500, 100, 0.5)", AbilityComp->GetAttributeValueByTag(ManaTag), 400.f);
			Effect->RemoveFromRoot();
		});

		It("AddPercentageOfAttributeRawValue reads ValueAsAttribute's RawValue when set", [this]()
		{
			AbilityComp->BindReplicationData();
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = EffectTag;
			FGMCAttributeModifier Mod; Mod.AttributeTag = HealthTag; Mod.Op = EModifierType::AddPercentageOfAttributeRawValue;
			Mod.ValueType = EGMCAttributeModifierType::AMT_Value; Mod.ModifierValue = 10.f; Mod.ValueAsAttribute = MaxTag;   // Max raw = 500
			Data.Modifiers.Add(Mod);
			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ProcessAttributes(true);
			TestEqual("100 + 10% of 500", AbilityComp->GetAttributeValueByTag(HealthTag), 150.f);
			Effect->RemoveFromRoot();
		});

		It("AMT_Custom with a null class contributes 0 and logs", [this]()
		{
			AddExpectedErrorPlain(TEXT("has no CustomModifierClass"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->BindReplicationData();
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Instant; Data.EffectTag = EffectTag;
			FGMCAttributeModifier Mod; Mod.AttributeTag = HealthTag; Mod.Op = EModifierType::Add; Mod.ValueType = EGMCAttributeModifierType::AMT_Custom;
			Data.Modifiers.Add(Mod);
			AbilityComp->ApplyAbilityEffect(Effect, Data);
			AbilityComp->ProcessAttributes(true);
			TestEqual("unchanged", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
