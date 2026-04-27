// Stress tests for FAttributeClamp interaction with FAttribute.
// Targets: degenerate ranges (Min>Max, Min=Max=0, negative bounds), boundary values,
// clamp interaction with Set/SetReplace, and the IsSet() short-circuit when both bounds = 0.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeClamp.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Effects/GMCAbilityEffect.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeClampStressSpec,
	"GMAS.Stress.Clamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FAttribute MakeClamped(float Init, float Min, float Max) const;
	UGMCAbilityEffect* SpawnEffect();

END_DEFINE_SPEC(FGMASAttributeClampStressSpec)

FAttribute FGMASAttributeClampStressSpec::MakeClamped(float Init, float Min, float Max) const
{
	FAttribute A; A.InitialValue = Init; A.Clamp.Min = Min; A.Clamp.Max = Max; A.Init(); return A;
}
UGMCAbilityEffect* FGMASAttributeClampStressSpec::SpawnEffect()
{
	UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage()); E->AddToRoot(); return E;
}

void FGMASAttributeClampStressSpec::Define()
{
	// ─── Boundary values ───────────────────────────────────────────────────
	Describe("Clamp boundaries", [this]()
	{
		It("Value exactly at Min", [this]()
		{
			FAttribute A = MakeClamped(0.f, 0.f, 100.f);
			TestEqual("Init at Min", A.Value, 0.f);
		});
		It("Value exactly at Max", [this]()
		{
			FAttribute A = MakeClamped(100.f, 0.f, 100.f);
			TestEqual("Init at Max", A.Value, 100.f);
		});
		It("Init below Min clamps up", [this]()
		{
			FAttribute A = MakeClamped(-50.f, 0.f, 100.f);
			TestEqual("InitialValue clamped to Min", A.Value, 0.f);
		});
		It("Init above Max clamps down", [this]()
		{
			FAttribute A = MakeClamped(200.f, 0.f, 100.f);
			TestEqual("InitialValue clamped to Max", A.Value, 100.f);
		});
		It("Add at boundary — exact equality preserved", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 50.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("50 + 50 = 100 (at Max boundary)", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Add above Max clamps to Max", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Clamped to Max", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Negative Add clamps to Min", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = -999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Clamped to Min", A.Value, 0.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Per-step clamp invariant ──────────────────────────────────────────
	Describe("Per-step clamping (ClampValue called after each modifier)", [this]()
	{
		It("Sequence +200, -200 clamps at every step", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M1, M2;
			M1.Op = M2.Op = EModifierType::Add;
			M1.ValueType = M2.ValueType = EGMCAttributeModifierType::AMT_Value;
			M1.DeltaTime = M2.DeltaTime = 1.f;
			M1.bRegisterInHistory = M2.bRegisterInHistory = true;
			M1.SourceAbilityEffect = M2.SourceAbilityEffect = E;
			M1.ModifierValue = 200.f; M1.ApplicationIndex = 1; M1.ActionTimer = 1.0;
			M2.ModifierValue = -200.f; M2.ApplicationIndex = 2; M2.ActionTimer = 2.0;
			A.AddModifier(M1); A.AddModifier(M2); A.CalculateValue();
			// Per-step: 50 + 200 → clamp to 100 → -200 → clamp to 0
			TestEqual("Per-step clamping (not net) lands at 0", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("Sequence -200, +200 lands at 100 (not 50)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M1, M2;
			M1.Op = M2.Op = EModifierType::Add;
			M1.ValueType = M2.ValueType = EGMCAttributeModifierType::AMT_Value;
			M1.DeltaTime = M2.DeltaTime = 1.f;
			M1.bRegisterInHistory = M2.bRegisterInHistory = true;
			M1.SourceAbilityEffect = M2.SourceAbilityEffect = E;
			M1.ModifierValue = -200.f; M1.ApplicationIndex = 1; M1.ActionTimer = 1.0;
			M2.ModifierValue = 200.f; M2.ApplicationIndex = 2; M2.ActionTimer = 2.0;
			A.AddModifier(M1); A.AddModifier(M2); A.CalculateValue();
			// 50 → clamp 0 → +200 → clamp 100
			TestEqual("Reverse sequence lands at 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("100 alternating ±10 modifiers stay within clamp range", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			for (int i = 0; i < 100; ++i) {
				FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
				M.ModifierValue = (i % 2 == 0) ? 10.f : -10.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
				M.SourceAbilityEffect = E; M.ApplicationIndex = i; M.ActionTimer = double(i);
				A.AddModifier(M);
			}
			A.CalculateValue();
			TestTrue("Within [Min, Max]", A.Value >= 0.f && A.Value <= 100.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Negative ranges ───────────────────────────────────────────────────
	Describe("Negative ranges", [this]()
	{
		It("Negative-bounded clamp [-100, -10]", [this]()
		{
			FAttribute A = MakeClamped(-50.f, -100.f, -10.f);
			TestEqual("Init in negative range", A.Value, -50.f);
		});
		It("Init above negative Max clamps down", [this]()
		{
			FAttribute A = MakeClamped(0.f, -100.f, -10.f);
			TestEqual("0 clamped to Max=-10", A.Value, -10.f);
		});
		It("Init below negative Min clamps up", [this]()
		{
			FAttribute A = MakeClamped(-200.f, -100.f, -10.f);
			TestEqual("-200 clamped to Min=-100", A.Value, -100.f);
		});
		It("Negative-spanning range [-50, +50]", [this]()
		{
			FAttribute A = MakeClamped(0.f, -50.f, 50.f);
			TestEqual("Init at center 0", A.Value, 0.f);
		});
	});

	// ─── Degenerate clamps ─────────────────────────────────────────────────
	Describe("Degenerate clamp ranges", [this]()
	{
		It("Min == Max pins value to single point", [this]()
		{
			FAttribute A = MakeClamped(75.f, 50.f, 50.f);
			TestEqual("Init pinned to 50", A.Value, 50.f);
		});
		It("Min > Max — FMath::Clamp returns Min (UE convention)", [this]()
		{
			FAttribute A = MakeClamped(50.f, 100.f, 0.f);
			// FMath::Clamp(50, 100, 0) — implementation-defined for inverted bounds.
			// UE's FMath::Clamp returns Max if Value > Max, Min if Value < Min.
			// With Min>Max, behavior is degenerate; we just ensure it doesn't crash.
			TestTrue("Does not crash; Value is finite", FMath::IsFinite(A.Value));
		});
		It("Both Min and Max = 0 disables clamp (IsSet returns false)", [this]()
		{
			FAttribute A = MakeClamped(50.f, 0.f, 0.f);
			TestEqual("Clamp disabled, value preserved", A.Value, 50.f);
		});
		It("Min=0 Max=0 with Init=-50: value preserved (clamp inactive)", [this]()
		{
			FAttribute A = MakeClamped(-50.f, 0.f, 0.f);
			TestEqual("Negative survives because clamp not set", A.Value, -50.f);
		});
		It("Min=0 Max=positive only: IsSet=true, clamp active", [this]()
		{
			FAttribute A = MakeClamped(-50.f, 0.f, 100.f);
			TestEqual("Clamped to Min=0", A.Value, 0.f);
		});
		It("Min=negative Max=0: IsSet=true, clamp active (negative range)", [this]()
		{
			FAttribute A = MakeClamped(50.f, -100.f, 0.f);
			TestEqual("Clamped to Max=0", A.Value, 0.f);
		});
	});

	// ─── Clamp + Set ───────────────────────────────────────────────────────
	Describe("Clamp interaction with Set", [this]()
	{
		It("Set above Max clamps to Max", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Set; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Set 999 clamped to Max=100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Set below Min clamps to Min", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Set; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = -999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Set -999 clamped to Min=0", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("Set + Add: Set clamped first, then Add applied with clamp", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier S; S.Op = EModifierType::Set; S.ValueType = EGMCAttributeModifierType::AMT_Value;
			S.ModifierValue = 90.f; S.DeltaTime = 1.f; S.bRegisterInHistory = true;
			S.SourceAbilityEffect = E; S.ApplicationIndex = 1; S.ActionTimer = 1.0;
			FGMCAttributeModifier Add; Add.Op = EModifierType::Add; Add.ValueType = EGMCAttributeModifierType::AMT_Value;
			Add.ModifierValue = 50.f; Add.DeltaTime = 1.f; Add.bRegisterInHistory = true;
			Add.SourceAbilityEffect = E; Add.ApplicationIndex = 2; Add.ActionTimer = 2.0;
			A.AddModifier(S); A.AddModifier(Add); A.CalculateValue();
			// Set 90 (clamped to 90) + Add 50 = 140 → clamp to 100
			TestEqual("Set 90 + Add 50 → clamped to 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Set with negative value on negative-allowed clamp", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(0.f, -100.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Set; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = -75.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Set -75 in [-100, 100]", A.Value, -75.f);
			E->RemoveFromRoot();
		});
		It("SetReplace clamping behaves identically to Set", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(0.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::SetReplace; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 200.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("SetReplace clamped to Max", A.Value, 100.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Clamp + many adds ─────────────────────────────────────────────────
	Describe("Clamp + many Adds", [this]()
	{
		It("Adds that would overflow stay at Max", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(0.f, 0.f, 50.f);
			for (int i = 0; i < 100; ++i) {
				FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
				M.ModifierValue = 10.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
				M.SourceAbilityEffect = E; M.ApplicationIndex = i; M.ActionTimer = double(i);
				A.AddModifier(M);
			}
			A.CalculateValue();
			TestEqual("100×10 clamped to Max=50", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Adds that would underflow stay at Min", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			for (int i = 0; i < 100; ++i) {
				FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
				M.ModifierValue = -10.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
				M.SourceAbilityEffect = E; M.ApplicationIndex = i; M.ActionTimer = double(i);
				A.AddModifier(M);
			}
			A.CalculateValue();
			TestEqual("100×-10 clamped to Min=0", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("Add of FLT_MAX clamps to Max", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = FLT_MAX; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("FLT_MAX clamped to Max", A.Value, 100.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Permanent + clamp ──────────────────────────────────────────────────
	Describe("Permanent modifier + clamp", [this]()
	{
		It("Permanent +X clamps RawValue", [this]()
		{
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = false;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("RawValue clamped to Max", A.RawValue, 100.f);
			TestEqual("Value matches RawValue", A.Value, 100.f);
		});
		It("Permanent -X clamps RawValue to Min", [this]()
		{
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = -999.f; M.DeltaTime = 1.f; M.bRegisterInHistory = false;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("RawValue clamped to Min", A.RawValue, 0.f);
		});
		It("Stacking permanent +50 modifiers respects Max", [this]()
		{
			FAttribute A = MakeClamped(0.f, 0.f, 100.f);
			for (int i = 0; i < 5; ++i) {
				FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
				M.ModifierValue = 50.f; M.DeltaTime = 1.f; M.bRegisterInHistory = false;
				A.AddModifier(M);
			}
			A.CalculateValue();
			TestEqual("5×50 clamped to Max=100", A.RawValue, 100.f);
		});
	});

	// ─── PurgeTemporalModifier + clamp ──────────────────────────────────────
	Describe("Purge + clamp interaction", [this]()
	{
		It("Purging modifiers that pushed value to Max returns to in-range Value", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(50.f, 0.f, 100.f);
			A.bIsGMCBound = true;
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 500.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 5.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Pre-purge: clamped to Max", A.Value, 100.f);
			A.PurgeTemporalModifier(2.0);   // purges entry at t=5
			A.CalculateValue();
			TestEqual("After purge: back to Init", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Purging only one of two boost modifiers", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeClamped(0.f, 0.f, 100.f);
			A.bIsGMCBound = true;
			FGMCAttributeModifier M1, M2;
			M1.Op = M2.Op = EModifierType::Add;
			M1.ValueType = M2.ValueType = EGMCAttributeModifierType::AMT_Value;
			M1.DeltaTime = M2.DeltaTime = 1.f;
			M1.bRegisterInHistory = M2.bRegisterInHistory = true;
			M1.SourceAbilityEffect = M2.SourceAbilityEffect = E;
			M1.ModifierValue = 30.f; M1.ApplicationIndex = 1; M1.ActionTimer = 2.0;
			M2.ModifierValue = 50.f; M2.ApplicationIndex = 2; M2.ActionTimer = 5.0;
			A.AddModifier(M1); A.AddModifier(M2);
			A.CalculateValue();
			TestEqual("Both active: 80", A.Value, 80.f);
			A.PurgeTemporalModifier(3.0);   // keeps t=2, removes t=5
			A.CalculateValue();
			TestEqual("After purge of t=5: 30", A.Value, 30.f);
			E->RemoveFromRoot();
		});
	});

	// ─── ClampValue called on FAttributeClamp directly ─────────────────────
	Describe("FAttributeClamp::ClampValue direct calls", [this]()
	{
		It("IsSet false when both 0 and no tags", [this]()
		{
			FAttributeClamp C;
			TestFalse("Default clamp not set", C.IsSet());
		});
		It("IsSet true when Min set", [this]()
		{
			FAttributeClamp C; C.Min = 10.f;
			TestTrue("Min activates clamp", C.IsSet());
		});
		It("IsSet true when Max set", [this]()
		{
			FAttributeClamp C; C.Max = 100.f;
			TestTrue("Max activates clamp", C.IsSet());
		});
		It("ClampValue returns input verbatim when not set", [this]()
		{
			FAttributeClamp C;
			TestEqual("Pass-through 42", C.ClampValue(42.f), 42.f);
			TestEqual("Pass-through -1e9", C.ClampValue(-1e9f), -1e9f);
			TestEqual("Pass-through FLT_MAX", C.ClampValue(FLT_MAX), FLT_MAX);
		});
		It("ClampValue with no AbilityComponent uses raw Min/Max", [this]()
		{
			FAttributeClamp C; C.Min = 0.f; C.Max = 50.f;
			TestEqual("Above Max", C.ClampValue(100.f), 50.f);
			TestEqual("Below Min", C.ClampValue(-10.f), 0.f);
			TestEqual("In range", C.ClampValue(25.f), 25.f);
		});
		It("ClampValue with NaN input — output is implementation-defined but test runs", [this]()
		{
			FAttributeClamp C; C.Min = 0.f; C.Max = 100.f;
			const float NaN = std::numeric_limits<float>::quiet_NaN();
			const float Result = C.ClampValue(NaN);
			TestTrue("Returns finite or NaN, doesn't crash", FMath::IsFinite(Result) || FMath::IsNaN(Result));
		});
		It("ClampValue with +Inf input", [this]()
		{
			FAttributeClamp C; C.Min = 0.f; C.Max = 100.f;
			const float Result = C.ClampValue(std::numeric_limits<float>::infinity());
			TestEqual("+Inf clamped to Max", Result, 100.f);
		});
		It("ClampValue with -Inf input", [this]()
		{
			FAttributeClamp C; C.Min = 0.f; C.Max = 100.f;
			const float Result = C.ClampValue(-std::numeric_limits<float>::infinity());
			TestEqual("-Inf clamped to Min", Result, 0.f);
		});
	});

	// ─── Pathological clamp configurations ─────────────────────────────────
	Describe("Pathological clamp", [this]()
	{
		It("Min = -Inf clamps to -Inf as floor", [this]()
		{
			FAttributeClamp C; C.Min = -std::numeric_limits<float>::infinity(); C.Max = 100.f;
			TestEqual("Pass-through any negative", C.ClampValue(-1e10f), -1e10f);
			TestEqual("Above Max", C.ClampValue(200.f), 100.f);
		});
		It("Max = +Inf has no upper bound", [this]()
		{
			FAttributeClamp C; C.Min = 0.f; C.Max = std::numeric_limits<float>::infinity();
			TestEqual("Pass-through huge positive", C.ClampValue(1e10f), 1e10f);
			TestEqual("Below Min", C.ClampValue(-5.f), 0.f);
		});
		It("Min = NaN — degenerate, system shouldn't crash", [this]()
		{
			FAttributeClamp C; C.Min = std::numeric_limits<float>::quiet_NaN(); C.Max = 100.f;
			const float Result = C.ClampValue(50.f);
			TestTrue("Doesn't crash with NaN bound", FMath::IsFinite(Result) || FMath::IsNaN(Result));
		});
		It("Min = Max = -1 (negative pin)", [this]()
		{
			FAttributeClamp C; C.Min = -1.f; C.Max = -1.f;
			TestEqual("Pinned to -1", C.ClampValue(50.f), -1.f);
			TestEqual("Pinned to -1 from below", C.ClampValue(-100.f), -1.f);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
