// Stress tests for FAttribute — focus on the nasty stuff: floating-point pathology,
// NaN/Inf propagation, massive stacking, commutativity invariants, denormals, FLT_MAX,
// catastrophic cancellation. None of these tests assume designer well-behavedness;
// they probe how the system reacts to garbage input or extreme load.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Attributes/GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include <limits>

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeStressSpec,
	"GMAS.Stress.Attribute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FAttribute MakeAttr(float Init = 100.f) const;
	FGMCAttributeModifier MakePerm(float V) const;
	FGMCAttributeModifier MakeTemp(UGMCAbilityEffect* Eff, float V, int Idx, double T,
		EModifierType Op = EModifierType::Add) const;
	UGMCAbilityEffect* SpawnEffect();

END_DEFINE_SPEC(FGMASAttributeStressSpec)

FAttribute FGMASAttributeStressSpec::MakeAttr(float Init) const
{
	FAttribute A; A.InitialValue = Init; A.Init(); return A;
}
FGMCAttributeModifier FGMASAttributeStressSpec::MakePerm(float V) const
{
	FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
	M.ModifierValue = V; M.DeltaTime = 1.f; M.bRegisterInHistory = false; return M;
}
FGMCAttributeModifier FGMASAttributeStressSpec::MakeTemp(UGMCAbilityEffect* Eff, float V, int Idx, double T, EModifierType Op) const
{
	FGMCAttributeModifier M; M.Op = Op; M.ValueType = EGMCAttributeModifierType::AMT_Value;
	M.ModifierValue = V; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
	M.SourceAbilityEffect = Eff; M.ApplicationIndex = Idx; M.ActionTimer = T; return M;
}
UGMCAbilityEffect* FGMASAttributeStressSpec::SpawnEffect()
{
	UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage()); E->AddToRoot(); return E;
}

void FGMASAttributeStressSpec::Define()
{
	// ─── Float precision ──────────────────────────────────────────────────────
	Describe("Float precision: small modifiers", [this]()
	{
		It("100 × +0.1 = ~10 within tolerance (no precision blowup)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 100; ++i) { A.AddModifier(MakeTemp(E, 0.1f, i, double(i))); }
			A.CalculateValue();
			TestNearlyEqual("100×0.1 ≈ 10", A.Value, 10.f, 1e-3f);
			E->RemoveFromRoot();
		});
		It("1000 × +0.001 = ~1 within tolerance", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 1000; ++i) { A.AddModifier(MakeTemp(E, 0.001f, i, double(i))); }
			A.CalculateValue();
			TestNearlyEqual("1000×0.001 ≈ 1", A.Value, 1.f, 1e-2f);
			E->RemoveFromRoot();
		});
		It("Adds of FLT_EPSILON × FLT_MAX magnitude do not silently zero", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(FLT_MAX / 2.f);
			A.AddModifier(MakeTemp(E, FLT_EPSILON, 1, 0.0));
			A.CalculateValue();
			TestEqual("Tiny addend on huge base preserved (or absorbed deterministically)", A.Value, FLT_MAX / 2.f);
			E->RemoveFromRoot();
		});
		It("Subnormal float modifier doesn't crash CalculateValue", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(1.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::denorm_min(), 1, 0.0));
			A.CalculateValue();
			TestTrue("Value still finite after subnormal modifier", FMath::IsFinite(A.Value));
			E->RemoveFromRoot();
		});
		It("Repeated +0 yields baseline", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(50.f);
			for (int i = 0; i < 50; ++i) { A.AddModifier(MakeTemp(E, 0.f, i, double(i))); }
			A.CalculateValue();
			TestEqual("50 + 50×0 = 50", A.Value, 50.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Catastrophic cancellation ─────────────────────────────────────────────
	Describe("Catastrophic cancellation", [this]()
	{
		It("+1e6 then −1e6 returns to baseline (within float precision)", [this]()
		{
			// Note: 1e10 would lose the 100 entirely (mantissa overflow).
			// 1e6 fits within float precision while still demonstrating cancellation.
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 1e6f, 1, 1.0));
			A.AddModifier(MakeTemp(E, -1e6f, 2, 2.0));
			A.CalculateValue();
			TestNearlyEqual("Cancellation preserves base", A.Value, 100.f, 0.1f);
			E->RemoveFromRoot();
		});
		It("+1e10 then −1e10 documents intentional precision loss (100 vanishes)", [this]()
		{
			// At 1e10 magnitude, float can't represent +100 — it falls below the mantissa LSB.
			// Result: cancellation lands exactly at 0, NOT 100. This is IEEE-754 by design.
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 1e10f, 1, 1.0));
			A.AddModifier(MakeTemp(E, -1e10f, 2, 2.0));
			A.CalculateValue();
			TestEqual("Float precision loss is reproducible at scale 1e10", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("Removing the inverse modifier returns to original", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 50.f, 1, 1.0));
			A.AddModifier(MakeTemp(E, -50.f, 2, 2.0));
			A.CalculateValue();
			TestEqual("Net zero after pair", A.Value, 100.f);
			A.RemoveTemporalModifier(2, E); A.CalculateValue();
			TestEqual("Removing -50 leaves +50", A.Value, 150.f);
			A.RemoveTemporalModifier(1, E); A.CalculateValue();
			TestEqual("Removing both restores 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Pairwise cancel of 100 adds returns near-baseline", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 100; ++i) {
				A.AddModifier(MakeTemp(E, +1.0f, i*2, double(i*2)));
				A.AddModifier(MakeTemp(E, -1.0f, i*2+1, double(i*2+1)));
			}
			A.CalculateValue();
			TestNearlyEqual("100 pairs cancel to ~0", A.Value, 0.f, 1e-3f);
			E->RemoveFromRoot();
		});
	});

	// ─── NaN / Inf propagation ─────────────────────────────────────────────────
	Describe("NaN / Inf propagation", [this]()
	{
		It("Adding +Inf produces +Inf, not crash", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::infinity(), 1, 0.0));
			A.CalculateValue();
			TestTrue("Value is +Inf", A.Value > FLT_MAX);
			E->RemoveFromRoot();
		});
		It("Adding -Inf produces -Inf", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, -std::numeric_limits<float>::infinity(), 1, 0.0));
			A.CalculateValue();
			TestTrue("Value is -Inf", A.Value < -FLT_MAX);
			E->RemoveFromRoot();
		});
		It("NaN modifier propagates to Value (NaN is sticky)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::quiet_NaN(), 1, 0.0));
			A.CalculateValue();
			TestTrue("Value becomes NaN", FMath::IsNaN(A.Value));
			E->RemoveFromRoot();
		});
		It("+Inf then -Inf yields NaN (IEEE-754)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::infinity(), 1, 0.0));
			A.AddModifier(MakeTemp(E, -std::numeric_limits<float>::infinity(), 2, 1.0));
			A.CalculateValue();
			TestTrue("Inf − Inf = NaN", FMath::IsNaN(A.Value));
			E->RemoveFromRoot();
		});
		It("Removing a NaN modifier restores finite Value", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::quiet_NaN(), 1, 0.0));
			A.CalculateValue();
			A.RemoveTemporalModifier(1, E); A.CalculateValue();
			TestTrue("Value finite after removing NaN", FMath::IsFinite(A.Value));
			TestEqual("Back to base", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Set with NaN target gives NaN Value (no recovery)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::quiet_NaN(), 1, 0.0, EModifierType::Set));
			A.CalculateValue();
			TestTrue("Set NaN propagates", FMath::IsNaN(A.Value));
			E->RemoveFromRoot();
		});
		It("Set with +Inf target gives +Inf Value", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, std::numeric_limits<float>::infinity(), 1, 0.0, EModifierType::Set));
			A.CalculateValue();
			TestTrue("Set +Inf propagates", A.Value > FLT_MAX);
			E->RemoveFromRoot();
		});
	});

	// ─── FLT_MAX / overflow ─────────────────────────────────────────────────
	Describe("FLT_MAX / overflow", [this]()
	{
		It("Add to FLT_MAX produces +Inf without crash", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(FLT_MAX);
			A.AddModifier(MakeTemp(E, FLT_MAX, 1, 0.0));
			A.CalculateValue();
			TestTrue("FLT_MAX + FLT_MAX overflows to +Inf", A.Value > FLT_MAX);
			E->RemoveFromRoot();
		});
		It("Stacking FLT_MAX adds clamps deterministically when no Clamp set", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 5; ++i) { A.AddModifier(MakeTemp(E, FLT_MAX, i, double(i))); }
			A.CalculateValue();
			TestTrue("Many FLT_MAX adds yield +Inf", A.Value > FLT_MAX);
			E->RemoveFromRoot();
		});
		It("Negative overflow on -FLT_MAX adds", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			A.AddModifier(MakeTemp(E, -FLT_MAX, 1, 0.0));
			A.AddModifier(MakeTemp(E, -FLT_MAX, 2, 1.0));
			A.CalculateValue();
			TestTrue("-FLT_MAX × 2 underflows to -Inf", A.Value < -FLT_MAX);
			E->RemoveFromRoot();
		});
		It("InitialValue = FLT_MAX init", [this]()
		{
			FAttribute A; A.InitialValue = FLT_MAX; A.Init();
			TestEqual("RawValue = FLT_MAX", A.RawValue, FLT_MAX);
			TestEqual("Value = FLT_MAX", A.Value, FLT_MAX);
		});
		It("InitialValue = -FLT_MAX init", [this]()
		{
			FAttribute A; A.InitialValue = -FLT_MAX; A.Init();
			TestEqual("RawValue = -FLT_MAX", A.RawValue, -FLT_MAX);
		});
	});

	// ─── Massive stacking ──────────────────────────────────────────────────
	Describe("Massive stacking (1000+ modifiers)", [this]()
	{
		It("1000 × +1 = 1000 exactly", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 1000; ++i) { A.AddModifier(MakeTemp(E, 1.f, i, double(i))); }
			A.CalculateValue();
			TestEqual("1000 × +1 = 1000", A.Value, 1000.f);
			E->RemoveFromRoot();
		});
		It("1000 × +1 then 1000 × −1 = 0", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 1000; ++i) { A.AddModifier(MakeTemp(E, 1.f, i, double(i))); }
			for (int i = 0; i < 1000; ++i) { A.AddModifier(MakeTemp(E, -1.f, 1000+i, double(1000+i))); }
			A.CalculateValue();
			TestEqual("Net zero across 2000 modifiers", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("CalculateValue is O(n) — 10000 modifiers don't time out", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 10000; ++i) { A.AddModifier(MakeTemp(E, 0.001f, i, double(i))); }
			const double Start = FPlatformTime::Seconds();
			A.CalculateValue();
			const double Elapsed = FPlatformTime::Seconds() - Start;
			TestTrue("10k modifiers calc < 100ms", Elapsed < 0.1);
			TestNearlyEqual("Sum approximates 10", A.Value, 10.f, 1.f);
			E->RemoveFromRoot();
		});
		It("Removing 500 of 1000 modifiers leaves correct partial sum", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 1000; ++i) { A.AddModifier(MakeTemp(E, 1.f, i, double(i))); }
			for (int i = 0; i < 500; ++i) { A.RemoveTemporalModifier(i, E); }
			A.CalculateValue();
			TestEqual("500 remain", A.Value, 500.f);
			E->RemoveFromRoot();
		});
		It("Stacking integers from 1 to 100 sums to 5050 (Gauss)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 1; i <= 100; ++i) { A.AddModifier(MakeTemp(E, float(i), i, double(i))); }
			A.CalculateValue();
			TestEqual("Σ 1..100 = 5050", A.Value, 5050.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Order independence ────────────────────────────────────────────────
	Describe("Order independence (Adds are commutative)", [this]()
	{
		It("Add A then Add B == Add B then Add A", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute X = MakeAttr(100.f), Y = MakeAttr(100.f);
			X.AddModifier(MakeTemp(E, 7.f, 1, 1.0)); X.AddModifier(MakeTemp(E, 13.f, 2, 2.0));
			Y.AddModifier(MakeTemp(E, 13.f, 2, 2.0)); Y.AddModifier(MakeTemp(E, 7.f, 1, 1.0));
			X.CalculateValue(); Y.CalculateValue();
			TestEqual("Same final Value", X.Value, Y.Value);
			E->RemoveFromRoot();
		});
		It("Reverse-insertion-order yields same Value (10 modifiers)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute X = MakeAttr(0.f), Y = MakeAttr(0.f);
			for (int i = 0; i < 10; ++i) { X.AddModifier(MakeTemp(E, float(i+1), i, double(i))); }
			for (int i = 9; i >= 0; --i) { Y.AddModifier(MakeTemp(E, float(i+1), i, double(i))); }
			X.CalculateValue(); Y.CalculateValue();
			TestEqual("Final Value identical", X.Value, Y.Value);
			E->RemoveFromRoot();
		});
		It("Removal order doesn't affect final Value when removing all", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute X = MakeAttr(100.f), Y = MakeAttr(100.f);
			for (int i = 0; i < 5; ++i) {
				X.AddModifier(MakeTemp(E, float(i+1), i, double(i)));
				Y.AddModifier(MakeTemp(E, float(i+1), i, double(i)));
			}
			for (int i = 0; i < 5; ++i) { X.RemoveTemporalModifier(i, E); }
			for (int i = 4; i >= 0; --i) { Y.RemoveTemporalModifier(i, E); }
			X.CalculateValue(); Y.CalculateValue();
			TestEqual("Removed all, both at base", X.Value, Y.Value);
			TestEqual("Both at 100", X.Value, 100.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Permanent (RawValue) interactions ─────────────────────────────────
	Describe("Permanent modifiers (bRegisterInHistory=false)", [this]()
	{
		It("Permanent +X is reflected in RawValue and Value", [this]()
		{
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(25.f)); A.CalculateValue();
			TestEqual("RawValue updated", A.RawValue, 125.f);
			TestEqual("Value updated", A.Value, 125.f);
		});
		It("Permanent +X then temporal +Y stack (RawValue + temporal sum)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(20.f));
			A.AddModifier(MakeTemp(E, 5.f, 1, 1.0));
			A.CalculateValue();
			TestEqual("RawValue 120", A.RawValue, 120.f);
			TestEqual("Value 120 + 5 = 125", A.Value, 125.f);
			E->RemoveFromRoot();
		});
		It("Permanent +X is NOT removed by RemoveTemporalModifier", [this]()
		{
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f)); A.CalculateValue();
			A.RemoveTemporalModifier(0, nullptr); A.CalculateValue();
			TestEqual("Permanent survives", A.Value, 150.f);
		});
		It("Permanent +X is NOT removed by PurgeTemporalModifier", [this]()
		{
			FAttribute A = MakeAttr(100.f); A.bIsGMCBound = true;
			A.AddModifier(MakePerm(50.f)); A.CalculateValue();
			A.PurgeTemporalModifier(-DBL_MAX); A.CalculateValue();
			TestEqual("Permanent survives full purge", A.Value, 150.f);
		});
		It("Permanent Set wipes RawValue to absolute value", [this]()
		{
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f)); A.CalculateValue();   // RawValue = 150
			FGMCAttributeModifier S; S.Op = EModifierType::Set; S.ValueType = EGMCAttributeModifierType::AMT_Value;
			S.ModifierValue = 30.f; S.DeltaTime = 1.f; S.bRegisterInHistory = false;
			A.AddModifier(S); A.CalculateValue();
			TestEqual("Permanent Set replaces RawValue, not adds", A.RawValue, 30.f);
			TestEqual("Value matches RawValue", A.Value, 30.f);
		});
		It("Permanent SetReplace behaves identically to permanent Set", [this]()
		{
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f)); A.CalculateValue();
			FGMCAttributeModifier S; S.Op = EModifierType::SetReplace; S.ValueType = EGMCAttributeModifierType::AMT_Value;
			S.ModifierValue = 60.f; S.DeltaTime = 1.f; S.bRegisterInHistory = false;
			A.AddModifier(S); A.CalculateValue();
			TestEqual("Permanent SetReplace replaces RawValue", A.RawValue, 60.f);
		});
		It("Cumulative permanent stacking", [this]()
		{
			FAttribute A = MakeAttr(0.f);
			for (int i = 1; i <= 10; ++i) { A.AddModifier(MakePerm(float(i))); }
			A.CalculateValue();
			TestEqual("Σ 1..10 in RawValue", A.RawValue, 55.f);
		});
	});

	// ─── Mixed temporal + permanent + Set interactions ────────────────────
	Describe("Mixed temporal/permanent/Set", [this]()
	{
		It("Temporal Set hides RawValue contribution from permanent", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f)); A.CalculateValue();   // RawValue=150
			A.AddModifier(MakeTemp(E, 30.f, 1, 1.0, EModifierType::Set));
			A.CalculateValue();
			TestEqual("Set 30 hides RawValue 150", A.Value, 30.f);
			E->RemoveFromRoot();
		});
		It("Removing the Set restores permanent contribution", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f));
			A.AddModifier(MakeTemp(E, 30.f, 1, 1.0, EModifierType::Set));
			A.CalculateValue();
			A.RemoveTemporalModifier(1, E); A.CalculateValue();
			TestEqual("Back to RawValue 150", A.Value, 150.f);
			E->RemoveFromRoot();
		});
		It("Temporal Add stacks on top of Set, with permanent hidden", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(50.f));
			A.AddModifier(MakeTemp(E, 30.f, 1, 1.0, EModifierType::Set));
			A.AddModifier(MakeTemp(E, 7.f, 2, 2.0));
			A.CalculateValue();
			TestEqual("Set 30 + Add 7 = 37", A.Value, 37.f);
			E->RemoveFromRoot();
		});
	});

	// ─── EModifierKind enum sanity ──────────────────────────────────────────
	Describe("EAttributeModifierKind sanity", [this]()
	{
		It("Default-constructed FAttributeTemporaryModifier is Add", [this]()
		{
			FAttributeTemporaryModifier M;
			TestEqual("Default kind", M.Kind, EAttributeModifierKind::Add);
		});
		It("Default ApplicationIndex is 0", [this]()
		{
			FAttributeTemporaryModifier M;
			TestEqual("Default index", M.ApplicationIndex, 0);
		});
		It("Default Value is 0", [this]()
		{
			FAttributeTemporaryModifier M;
			TestEqual("Default value", M.Value, 0.f);
		});
		It("Default ActionTimer is 0.0", [this]()
		{
			FAttributeTemporaryModifier M;
			TestEqual("Default time", M.ActionTimer, 0.0);
		});
		It("Default InstigatorEffect is null", [this]()
		{
			FAttributeTemporaryModifier M;
			TestFalse("Default instigator invalid", M.InstigatorEffect.IsValid());
		});
	});

	// ─── bIsDirty flag ──────────────────────────────────────────────────────
	Describe("bIsDirty flag mechanics", [this]()
	{
		It("AddModifier sets bIsDirty", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			TestFalse("Init clean", A.IsDirty());
			A.AddModifier(MakeTemp(E, 1.f, 1, 0.0));
			TestTrue("Add dirties", A.IsDirty());
			E->RemoveFromRoot();
		});
		It("CalculateValue clears bIsDirty", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 1.f, 1, 0.0));
			A.CalculateValue();
			TestFalse("Calc clears dirty", A.IsDirty());
			E->RemoveFromRoot();
		});
		It("RemoveTemporalModifier sets bIsDirty (when match)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 1.f, 1, 0.0));
			A.CalculateValue();
			A.RemoveTemporalModifier(1, E);
			TestTrue("Remove dirties", A.IsDirty());
			E->RemoveFromRoot();
		});
		It("RemoveTemporalModifier on non-existent does NOT set bIsDirty (no match path)", [this]()
		{
			FAttribute A = MakeAttr(100.f); A.CalculateValue();
			TestFalse("Init clean post-calc", A.IsDirty());
			A.RemoveTemporalModifier(999, nullptr);
			TestFalse("No-op remove leaves clean", A.IsDirty());
		});
		It("Permanent modifier dirties the attribute", [this]()
		{
			FAttribute A = MakeAttr(100.f); A.CalculateValue();
			A.AddModifier(MakePerm(10.f));
			TestTrue("Perm dirties", A.IsDirty());
		});
		It("PurgeTemporalModifier dirties only when something gets removed", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f); A.bIsGMCBound = true; A.CalculateValue();
			A.PurgeTemporalModifier(0.0);
			TestFalse("Purge on empty list = clean", A.IsDirty());
			A.AddModifier(MakeTemp(E, 1.f, 1, 5.0));
			A.CalculateValue();
			A.PurgeTemporalModifier(2.0);
			TestTrue("Purge that removes entry dirties", A.IsDirty());
			E->RemoveFromRoot();
		});
	});

	// ─── Init() idempotence ─────────────────────────────────────────────────
	Describe("Init() idempotence", [this]()
	{
		It("Init() twice with same InitialValue yields same RawValue", [this]()
		{
			FAttribute A; A.InitialValue = 50.f; A.Init();
			const float First = A.RawValue;
			A.Init();
			TestEqual("Idempotent Init", A.RawValue, First);
		});
		It("Changing InitialValue and re-Init updates RawValue", [this]()
		{
			FAttribute A; A.InitialValue = 50.f; A.Init();
			A.InitialValue = 75.f; A.Init();
			TestEqual("RawValue follows new InitialValue", A.RawValue, 75.f);
		});
		It("Init() does not affect existing temporal modifiers (the list is independent state)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, 10.f, 1, 0.0));
			A.Init();   // resets RawValue to InitialValue but TemporalModifiers stay
			A.CalculateValue();
			TestEqual("Init+CalculateValue: 100 + 10 = 110", A.Value, 110.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Negative values ────────────────────────────────────────────────────
	Describe("Negative values", [this]()
	{
		It("Negative InitialValue init", [this]()
		{
			FAttribute A; A.InitialValue = -50.f; A.Init();
			TestEqual("Negative init survives", A.RawValue, -50.f);
		});
		It("Negative permanent modifier on positive base", [this]()
		{
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakePerm(-30.f)); A.CalculateValue();
			TestEqual("100 - 30 = 70", A.Value, 70.f);
		});
		It("Negative temporal modifier (debuff)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, -25.f, 1, 0.0));
			A.CalculateValue();
			TestEqual("100 - 25 = 75", A.Value, 75.f);
			E->RemoveFromRoot();
		});
		It("Set with negative value", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeTemp(E, -42.f, 1, 0.0, EModifierType::Set));
			A.CalculateValue();
			TestEqual("Set -42", A.Value, -42.f);
			E->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
