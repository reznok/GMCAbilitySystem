// Tests for FAttribute lifecycle: Init, permanent modifiers, temporal modifiers,
// RemoveTemporalModifier, and PurgeTemporalModifier.
//
// Temporal modifier tests require a live UGMCAbilityEffect — created via
// NewObject and kept alive with AddToRoot for the scope of the test.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Effects/GMCAbilityEffect.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeSpec,
	"GMAS.Unit.Attribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FAttribute MakeAttr(float InitVal) const;
	FGMCAttributeModifier MakePermanentMod(float ModVal) const;
	FGMCAttributeModifier MakeTemporalMod(UGMCAbilityEffect* Effect, float ModVal, int AppIdx, double ActionTimer) const;

END_DEFINE_SPEC(FGMASAttributeSpec)

FAttribute FGMASAttributeSpec::MakeAttr(float InitVal) const
{
	FAttribute Attr;
	Attr.InitialValue = InitVal;
	Attr.Init();
	return Attr;
}

FGMCAttributeModifier FGMASAttributeSpec::MakePermanentMod(float ModVal) const
{
	FGMCAttributeModifier Mod;
	Mod.Op = EModifierType::Add;
	Mod.ValueType = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = ModVal;
	Mod.DeltaTime = 1.f;
	Mod.bRegisterInHistory = false;
	return Mod;
}

FGMCAttributeModifier FGMASAttributeSpec::MakeTemporalMod(UGMCAbilityEffect* Effect, float ModVal, int AppIdx, double ActionTimer) const
{
	FGMCAttributeModifier Mod;
	Mod.Op = EModifierType::Add;
	Mod.ValueType = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = ModVal;
	Mod.DeltaTime = 1.f;
	Mod.bRegisterInHistory = true;
	Mod.SourceAbilityEffect = Effect;
	Mod.ApplicationIndex = AppIdx;
	Mod.ActionTimer = ActionTimer;
	return Mod;
}

void FGMASAttributeSpec::Define()
{
	Describe("Init", [this]()
	{
		It("sets Value and RawValue to InitialValue when no clamp is configured", [this]()
		{
			FAttribute Attr;
			Attr.InitialValue = 150.f;
			Attr.Init();
			TestEqual("Value == InitialValue after Init", Attr.Value, 150.f);
			TestEqual("RawValue == InitialValue after Init", Attr.RawValue, 150.f);
		});

		It("clamps InitialValue when a static clamp is set", [this]()
		{
			FAttribute Attr;
			Attr.InitialValue = 200.f;
			Attr.Clamp.Min = 0.f;
			Attr.Clamp.Max = 100.f;
			Attr.Init();
			TestEqual("InitialValue=200 clamped to Max=100", Attr.Value, 100.f);
			TestEqual("RawValue also clamped", Attr.RawValue, 100.f);
		});
	});

	Describe("Permanent modifiers (bRegisterInHistory=false)", [this]()
	{
		It("increases RawValue and Value after AddModifier + CalculateValue", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakePermanentMod(25.f));
			Attr.CalculateValue();
			TestEqual("Value increased to 125", Attr.Value, 125.f);
			TestEqual("RawValue increased to 125", Attr.RawValue, 125.f);
		});

		It("decreases Value correctly (damage / drain)", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakePermanentMod(-40.f));
			Attr.CalculateValue();
			TestEqual("Value decreased to 60", Attr.Value, 60.f);
		});

		It("stacks multiple permanent modifiers cumulatively", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakePermanentMod(10.f));
			Attr.AddModifier(MakePermanentMod(20.f));
			Attr.AddModifier(MakePermanentMod(-5.f));
			Attr.CalculateValue();
			TestEqual("100 + 10 + 20 - 5 = 125", Attr.Value, 125.f);
		});

		It("respects the static clamp when adding modifiers", [this]()
		{
			FAttribute Attr = MakeAttr(90.f);
			Attr.Clamp.Min = 0.f;
			Attr.Clamp.Max = 100.f;
			Attr.RawValue = 90.f; // Init clamps
			Attr.AddModifier(MakePermanentMod(50.f)); // would push to 140
			Attr.CalculateValue();
			TestEqual("RawValue clamped to Max=100", Attr.RawValue, 100.f);
			TestEqual("Value clamped to Max=100", Attr.Value, 100.f);
		});

		It("does not affect InitialValue — only RawValue changes", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakePermanentMod(999.f));
			Attr.CalculateValue();
			TestEqual("InitialValue unchanged", Attr.InitialValue, 100.f);
		});
	});

	Describe("Temporal modifiers (bRegisterInHistory=true)", [this]()
	{
		It("adds to Value on top of RawValue without changing RawValue", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakeTemporalMod(Effect, 30.f, 1, 1.0));
			Attr.CalculateValue();

			TestEqual("Value includes temporal modifier: 100+30=130", Attr.Value, 130.f);
			TestEqual("RawValue unchanged at 100", Attr.RawValue, 100.f);

			Effect->RemoveFromRoot();
		});

		It("stacks multiple temporal modifiers additively", [this]()
		{
			UGMCAbilityEffect* Eff1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Eff2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Eff1->AddToRoot();
			Eff2->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakeTemporalMod(Eff1, 20.f, 1, 1.0));
			Attr.AddModifier(MakeTemporalMod(Eff2, 15.f, 2, 2.0));
			Attr.CalculateValue();

			TestEqual("100 + 20 + 15 = 135", Attr.Value, 135.f);

			Eff1->RemoveFromRoot();
			Eff2->RemoveFromRoot();
		});

		It("mixes permanent and temporal modifiers correctly", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakePermanentMod(-20.f)); // permanent drain: RawValue=80
			Attr.AddModifier(MakeTemporalMod(Effect, 50.f, 1, 1.0)); // temp buff: +50 on top
			Attr.CalculateValue();

			TestEqual("RawValue=80 (permanent only)", Attr.RawValue, 80.f);
			TestEqual("Value=130 (80 raw + 50 temporal)", Attr.Value, 130.f);

			Effect->RemoveFromRoot();
		});

		It("applies static clamp to each temporal modifier addition", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute Attr = MakeAttr(80.f);
			Attr.Clamp.Min = 0.f;
			Attr.Clamp.Max = 100.f;
			Attr.RawValue = 80.f;
			Attr.AddModifier(MakeTemporalMod(Effect, 50.f, 1, 1.0)); // would push to 130
			Attr.CalculateValue();

			TestEqual("Value clamped to Max=100 after temporal buff", Attr.Value, 100.f);

			Effect->RemoveFromRoot();
		});
	});

	Describe("RemoveTemporalModifier", [this]()
	{
		It("removes the modifier matching ApplicationIndex and effect pointer", [this]()
		{
			UGMCAbilityEffect* Eff1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Eff2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Eff1->AddToRoot();
			Eff2->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakeTemporalMod(Eff1, 20.f, 1, 1.0)); // AppIdx=1
			Attr.AddModifier(MakeTemporalMod(Eff2, 30.f, 2, 2.0)); // AppIdx=2
			Attr.CalculateValue();
			TestEqual("Before remove: 100+20+30=150", Attr.Value, 150.f);

			Attr.RemoveTemporalModifier(1, Eff1); // remove the +20
			Attr.CalculateValue();
			TestEqual("After removing AppIdx=1: 100+30=130", Attr.Value, 130.f);

			Eff1->RemoveFromRoot();
			Eff2->RemoveFromRoot();
		});

		It("does not remove a modifier with matching index but different effect", [this]()
		{
			UGMCAbilityEffect* Eff1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Eff2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Eff1->AddToRoot();
			Eff2->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakeTemporalMod(Eff1, 20.f, 1, 1.0));
			Attr.CalculateValue();

			// Attempt removal with wrong effect pointer (same AppIdx, different object)
			Attr.RemoveTemporalModifier(1, Eff2);
			Attr.CalculateValue();
			TestEqual("Modifier not removed when effect doesn't match", Attr.Value, 120.f);

			Eff1->RemoveFromRoot();
			Eff2->RemoveFromRoot();
		});

		It("is idempotent — removing the same modifier twice has no effect", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.AddModifier(MakeTemporalMod(Effect, 20.f, 1, 1.0));
			Attr.CalculateValue();

			Attr.RemoveTemporalModifier(1, Effect);
			Attr.RemoveTemporalModifier(1, Effect); // second call should no-op
			Attr.CalculateValue();
			TestEqual("Value returns to base after double-remove", Attr.Value, 100.f);

			Effect->RemoveFromRoot();
		});
	});

	Describe("PurgeTemporalModifier", [this]()
	{
		It("removes modifiers with ActionTimer strictly greater than CurrentActionTimer", [this]()
		{
			UGMCAbilityEffect* EarlierEff = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* LaterEff   = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			EarlierEff->AddToRoot();
			LaterEff->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.bIsGMCBound = true;

			// ActionTimer=0.5 → before replay point 1.0, should survive
			Attr.AddModifier(MakeTemporalMod(EarlierEff, 10.f, 1, 0.5));
			// ActionTimer=2.0 → after replay point 1.0, should be purged
			Attr.AddModifier(MakeTemporalMod(LaterEff, 30.f, 2, 2.0));
			Attr.CalculateValue();
			TestEqual("Before purge: 100+10+30=140", Attr.Value, 140.f);

			Attr.PurgeTemporalModifier(1.0);
			Attr.CalculateValue();
			TestEqual("After purge at 1.0: 100+10=110 (later mod removed)", Attr.Value, 110.f);

			EarlierEff->RemoveFromRoot();
			LaterEff->RemoveFromRoot();
		});

		It("keeps modifiers with ActionTimer equal to CurrentActionTimer (boundary)", [this]()
		{
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.bIsGMCBound = true;
			Attr.AddModifier(MakeTemporalMod(Effect, 20.f, 1, 1.0)); // ActionTimer == purge point
			Attr.CalculateValue();

			Attr.PurgeTemporalModifier(1.0);
			Attr.CalculateValue();
			// ActionTimer=1.0 is NOT > 1.0, so it survives
			TestEqual("Modifier at exact boundary is kept", Attr.Value, 120.f);

			Effect->RemoveFromRoot();
		});

		It("removes all future modifiers when purging from time zero", [this]()
		{
			UGMCAbilityEffect* Eff1 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			UGMCAbilityEffect* Eff2 = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Eff1->AddToRoot();
			Eff2->AddToRoot();

			FAttribute Attr = MakeAttr(100.f);
			Attr.bIsGMCBound = true;
			Attr.AddModifier(MakeTemporalMod(Eff1, 10.f, 1, 0.1));
			Attr.AddModifier(MakeTemporalMod(Eff2, 20.f, 2, 0.2));
			Attr.CalculateValue();
			TestEqual("Before purge: 130", Attr.Value, 130.f);

			Attr.PurgeTemporalModifier(0.0);
			Attr.CalculateValue();
			TestEqual("All modifiers purged from time=0", Attr.Value, 100.f);

			Eff1->RemoveFromRoot();
			Eff2->RemoveFromRoot();
		});

		It("is a no-op when there are no temporal modifiers", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.bIsGMCBound = true;
			Attr.AddModifier(MakePermanentMod(50.f));
			Attr.CalculateValue();

			Attr.PurgeTemporalModifier(0.0);
			Attr.CalculateValue();
			// Permanent modifier lives in RawValue, not ValueTemporalModifiers — unaffected
			TestEqual("Permanent modifier survives purge", Attr.Value, 150.f);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
