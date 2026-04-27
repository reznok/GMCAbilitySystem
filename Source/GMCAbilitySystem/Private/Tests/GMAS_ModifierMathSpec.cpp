// Tests for FGMCAttributeModifier::CalculateModifierValue.
// Covers every EModifierType that does NOT require a live SourceAbilityEffect /
// AbilityComponent: Add, AddPercentageInitialValue, AddPercentageMissing,
// AddClampedBetween, AddScaledBetween, AddPercentageMaxClamp, AddPercentageMinClamp.
//
// All tests use ValueType=AMT_Value so GetValue() returns ModifierValue directly
// without touching any UObject chain.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Attributes/GMCAttributeClamp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASModifierMathSpec,
	"GMAS.Unit.ModifierMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	// Helpers shared across Describe blocks
	FAttribute MakeAttr(float InitVal) const;
	FGMCAttributeModifier MakeMod(EModifierType Op, float ModVal, float DeltaTime = 1.f) const;

END_DEFINE_SPEC(FGMASModifierMathSpec)

FAttribute FGMASModifierMathSpec::MakeAttr(float InitVal) const
{
	FAttribute Attr;
	Attr.InitialValue = InitVal;
	Attr.Init();
	return Attr;
}

FGMCAttributeModifier FGMASModifierMathSpec::MakeMod(EModifierType Op, float ModVal, float DeltaTime) const
{
	FGMCAttributeModifier Mod;
	Mod.Op = Op;
	Mod.ValueType = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = ModVal;
	Mod.DeltaTime = DeltaTime;
	return Mod;
}

void FGMASModifierMathSpec::Define()
{
	Describe("Add", [this]()
	{
		It("adds the modifier value with DeltaTime=1 (instant/permanent)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Add, 25.f);
			TestEqual("Add 25 to anything", Mod.CalculateModifierValue(Attr), 25.f);
		});

		It("scales by DeltaTime for ticking effects", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Add, 60.f, 1.f / 60.f);
			// 60 hp/s at 60hz → 1.f per tick
			TestNearlyEqual("60/s at 60hz is 1 per tick", Mod.CalculateModifierValue(Attr), 1.f, KINDA_SMALL_NUMBER);
		});

		It("applies negative values (damage/drain)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Add, -30.f);
			TestEqual("subtract 30", Mod.CalculateModifierValue(Attr), -30.f);
		});

		It("applies zero modifier without side effects", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Add, 0.f);
			TestEqual("zero modifier", Mod.CalculateModifierValue(Attr), 0.f);
		});
	});

	Describe("AddPercentageInitialValue", [this]()
	{
		It("adds a percentage of InitialValue regardless of current Value", [this]()
		{
			const FAttribute Attr = MakeAttr(200.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageInitialValue, 25.f); // 25%
			// 200 * 0.25 * 1.0 = 50
			TestEqual("25% of 200", Mod.CalculateModifierValue(Attr), 50.f);
		});

		It("is not affected by current RawValue — only InitialValue matters", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			// Deplete 50 permanently
			FGMCAttributeModifier Drain = MakeMod(EModifierType::Add, -50.f);
			Drain.bRegisterInHistory = false;
			Attr.AddModifier(Drain);
			Attr.CalculateValue();
			// Value is now 50, but InitialValue is still 100
			const FGMCAttributeModifier HealMod = MakeMod(EModifierType::AddPercentageInitialValue, 10.f); // 10%
			// Should return 100 * 0.10 = 10, not 50 * 0.10 = 5
			TestEqual("10% of initial 100, not current 50", HealMod.CalculateModifierValue(Attr), 10.f);
		});

		It("returns negative for negative percentage (debuff)", [this]()
		{
			const FAttribute Attr = MakeAttr(300.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageInitialValue, -50.f); // -50%
			TestEqual("-50% of 300 = -150", Mod.CalculateModifierValue(Attr), -150.f);
		});
	});

	Describe("AddPercentageMissing", [this]()
	{
		It("heals a percentage of the missing value (InitialValue - Value)", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			FGMCAttributeModifier Drain = MakeMod(EModifierType::Add, -40.f);
			Drain.bRegisterInHistory = false;
			Attr.AddModifier(Drain);
			Attr.CalculateValue();
			// Missing = 100 - 60 = 40; 50% of 40 = 20
			const FGMCAttributeModifier HealMod = MakeMod(EModifierType::AddPercentageMissing, 50.f);
			TestEqual("50% of 40 missing = 20", HealMod.CalculateModifierValue(Attr), 20.f);
		});

		It("returns zero when attribute is at full value (nothing missing)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f); // Value == InitialValue == 100
			const FGMCAttributeModifier HealMod = MakeMod(EModifierType::AddPercentageMissing, 100.f);
			TestEqual("full health: 0 missing, 0 healed", HealMod.CalculateModifierValue(Attr), 0.f);
		});

		It("returns the full missing amount at 100 percent", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			FGMCAttributeModifier Drain = MakeMod(EModifierType::Add, -75.f);
			Drain.bRegisterInHistory = false;
			Attr.AddModifier(Drain);
			Attr.CalculateValue(); // Value = 25, Missing = 75
			const FGMCAttributeModifier HealMod = MakeMod(EModifierType::AddPercentageMissing, 100.f);
			TestEqual("100% of 75 missing = 75", HealMod.CalculateModifierValue(Attr), 75.f);
		});
	});

	Describe("AddClampedBetween", [this]()
	{
		It("returns the value unchanged when it is within [X, Y]", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddClampedBetween, 15.f);
			Mod.X = 5.f;
			Mod.Y = 20.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("15 inside [5,20]", Mod.CalculateModifierValue(Attr), 15.f);
		});

		It("clamps below X up to X", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddClampedBetween, 2.f);
			Mod.X = 5.f;
			Mod.Y = 20.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("2 clamped to min 5", Mod.CalculateModifierValue(Attr), 5.f);
		});

		It("clamps above Y down to Y", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddClampedBetween, 99.f);
			Mod.X = 5.f;
			Mod.Y = 20.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("99 clamped to max 20", Mod.CalculateModifierValue(Attr), 20.f);
		});

		It("scales by DeltaTime after clamping", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddClampedBetween, 10.f, 0.5f);
			Mod.X = 0.f;
			Mod.Y = 20.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("10 * 0.5 DeltaTime = 5", Mod.CalculateModifierValue(Attr), 5.f);
		});
	});

	Describe("AddScaledBetween", [this]()
	{
		It("lerps between X and Y using ModifierValue as t", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddScaledBetween, 0.5f);
			Mod.X = 0.f;
			Mod.Y = 100.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("t=0.5 lerp [0,100] = 50", Mod.CalculateModifierValue(Attr), 50.f);
		});

		It("returns X when t=0", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddScaledBetween, 0.f);
			Mod.X = 10.f;
			Mod.Y = 90.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("t=0 returns X=10", Mod.CalculateModifierValue(Attr), 10.f);
		});

		It("returns Y when t=1", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddScaledBetween, 1.f);
			Mod.X = 10.f;
			Mod.Y = 90.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("t=1 returns Y=90", Mod.CalculateModifierValue(Attr), 90.f);
		});

		It("clamps t > 1 to the Y bound", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddScaledBetween, 2.f);
			Mod.X = 0.f;
			Mod.Y = 100.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("t=2 clamped to max 100", Mod.CalculateModifierValue(Attr), 100.f);
		});

		It("clamps t < 0 to the X bound", [this]()
		{
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddScaledBetween, -1.f);
			Mod.X = 0.f;
			Mod.Y = 100.f;
			const FAttribute Attr = MakeAttr(100.f);
			TestEqual("t=-1 clamped to min 0", Mod.CalculateModifierValue(Attr), 0.f);
		});
	});

	Describe("AddPercentageMaxClamp", [this]()
	{
		It("adds a percentage of the attribute's Max clamp value", [this]()
		{
			FAttribute Attr = MakeAttr(0.f);
			Attr.Clamp.Max = 200.f; // MaxAttributeTag left empty → reads Clamp.Max directly
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageMaxClamp, 10.f); // 10%
			// 200 * 0.10 * 1.0 = 20
			TestEqual("10% of Max=200 is 20", Mod.CalculateModifierValue(Attr), 20.f);
		});

		It("returns zero when Max clamp is zero (unset)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f); // Clamp.Max = 0 by default
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageMaxClamp, 50.f);
			TestEqual("50% of Max=0 is 0", Mod.CalculateModifierValue(Attr), 0.f);
		});
	});

	Describe("AddPercentageMinClamp", [this]()
	{
		It("adds a percentage of the attribute's Min clamp value", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			Attr.Clamp.Min = 20.f; // MinAttributeTag left empty → reads Clamp.Min directly
			FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageMinClamp, 50.f); // 50%
			// 20 * 0.50 * 1.0 = 10
			TestEqual("50% of Min=20 is 10", Mod.CalculateModifierValue(Attr), 10.f);
		});

		It("returns zero when Min clamp is zero (default)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f); // Clamp.Min = 0 by default
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::AddPercentageMinClamp, 100.f);
			TestEqual("100% of Min=0 is 0", Mod.CalculateModifierValue(Attr), 0.f);
		});
	});

	Describe("Set / SetReplace", [this]()
	{
		It("Set returns the absolute target value verbatim, ignoring DeltaTime", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Set, 42.f, 1.f / 60.f);
			TestEqual("Set is absolute, DeltaTime irrelevant", Mod.CalculateModifierValue(Attr), 42.f);
		});

		It("Set is unaffected by Attribute's current Value or InitialValue", [this]()
		{
			FAttribute Attr = MakeAttr(100.f);
			FGMCAttributeModifier Drain = MakeMod(EModifierType::Add, -75.f);
			Drain.bRegisterInHistory = false;
			Attr.AddModifier(Drain);
			Attr.CalculateValue(); // Value is now 25, RawValue is 25

			const FGMCAttributeModifier SetMod = MakeMod(EModifierType::Set, 80.f);
			TestEqual("Set returns 80 regardless of attribute state", SetMod.CalculateModifierValue(Attr), 80.f);
		});

		It("SetReplace returns the absolute target value (same payload as Set)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::SetReplace, 17.f);
			TestEqual("SetReplace returns 17", Mod.CalculateModifierValue(Attr), 17.f);
		});

		It("Set with negative target value works (no scaling, no abs)", [this]()
		{
			const FAttribute Attr = MakeAttr(100.f);
			const FGMCAttributeModifier Mod = MakeMod(EModifierType::Set, -50.f);
			TestEqual("Set -50 returns -50", Mod.CalculateModifierValue(Attr), -50.f);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
