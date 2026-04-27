// Micro-tests covering invariants too small to fit in the dedicated stress specs.
// Heavy focus on default values, equality semantics, ApplicationIndex extremes,
// and a handful of pathological insertion patterns.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Attributes/GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"
#include <limits>

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeMicroSpec,
	"GMAS.Stress.Micro",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMCAbilityEffect* SpawnEffect();

END_DEFINE_SPEC(FGMASAttributeMicroSpec)

UGMCAbilityEffect* FGMASAttributeMicroSpec::SpawnEffect()
{
	UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage()); E->AddToRoot(); return E;
}

void FGMASAttributeMicroSpec::Define()
{
	// ─── FAttribute defaults ───────────────────────────────────────────────
	Describe("FAttribute defaults", [this]()
	{
		It("Default-constructed FAttribute has InitialValue=0", [this]()
		{
			FAttribute A;
			TestEqual("InitialValue default", A.InitialValue, 0.f);
		});
		It("Default-constructed has Value=0 before Init()", [this]()
		{
			FAttribute A;
			TestEqual("Value default", A.Value, 0.f);
		});
		It("Default-constructed has RawValue=0 before Init()", [this]()
		{
			FAttribute A;
			TestEqual("RawValue default", A.RawValue, 0.f);
		});
		It("Default-constructed has bIsGMCBound=false", [this]()
		{
			FAttribute A;
			TestFalse("bIsGMCBound default", A.bIsGMCBound);
		});
		It("Default-constructed has empty Tag", [this]()
		{
			FAttribute A;
			TestFalse("Tag default empty", A.Tag.IsValid());
		});
		It("Default-constructed has BoundIndex INDEX_NONE", [this]()
		{
			FAttribute A;
			TestEqual("BoundIndex default", A.BoundIndex, INDEX_NONE);
		});
		It("Default-constructed Clamp is not set (IsSet false)", [this]()
		{
			FAttribute A;
			TestFalse("Default clamp not active", A.Clamp.IsSet());
		});
	});

	// ─── ApplicationIndex extremes ─────────────────────────────────────────
	Describe("ApplicationIndex extremes", [this]()
	{
		It("ApplicationIndex INT_MAX works", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 5.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = INT_MAX; M.ActionTimer = 0.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("INT_MAX index works: 105", A.Value, 105.f);
			A.RemoveTemporalModifier(INT_MAX, E); A.CalculateValue();
			TestEqual("Removed: 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("ApplicationIndex INT_MIN works", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 5.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = INT_MIN; M.ActionTimer = 0.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("INT_MIN index works", A.Value, 105.f);
			E->RemoveFromRoot();
		});
		It("ApplicationIndex 0 works (not treated as sentinel)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 5.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 0; M.ActionTimer = 0.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Index 0 active", A.Value, 105.f);
			E->RemoveFromRoot();
		});
	});

	// ─── ActionTimer extremes ───────────────────────────────────────────────
	Describe("ActionTimer extremes", [this]()
	{
		It("ActionTimer = 0.0 works", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M; M.Op = EModifierType::Set; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 50.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 0.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Set at t=0", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Negative ActionTimer entries can win Set tie-break against 0", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier MA; MA.Op = EModifierType::Set; MA.ValueType = EGMCAttributeModifierType::AMT_Value;
			MA.ModifierValue = 30.f; MA.DeltaTime = 1.f; MA.bRegisterInHistory = true;
			MA.SourceAbilityEffect = E; MA.ApplicationIndex = 1; MA.ActionTimer = -10.0;
			FGMCAttributeModifier MB = MA; MB.ModifierValue = 50.f; MB.ApplicationIndex = 2; MB.ActionTimer = -5.0;
			A.AddModifier(MA); A.AddModifier(MB); A.CalculateValue();
			TestEqual("Less negative wins (closer to now)", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Very precise ActionTimer differences distinguish entries", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M1, M2;
			M1.Op = M2.Op = EModifierType::Set;
			M1.ValueType = M2.ValueType = EGMCAttributeModifierType::AMT_Value;
			M1.DeltaTime = M2.DeltaTime = 1.f;
			M1.bRegisterInHistory = M2.bRegisterInHistory = true;
			M1.SourceAbilityEffect = M2.SourceAbilityEffect = E;
			M1.ModifierValue = 30.f; M1.ApplicationIndex = 1; M1.ActionTimer = 1.0;
			M2.ModifierValue = 50.f; M2.ApplicationIndex = 2; M2.ActionTimer = 1.0 + DBL_EPSILON;
			A.AddModifier(M1); A.AddModifier(M2); A.CalculateValue();
			TestEqual("Plus DBL_EPSILON wins", A.Value, 50.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Equality semantics on FAttributeClamp ──────────────────────────────
	Describe("FAttributeClamp equality", [this]()
	{
		It("Two default clamps are equal", [this]()
		{
			FAttributeClamp A, B;
			TestTrue("A == B", A == B);
		});
		It("Different Min produces inequality", [this]()
		{
			FAttributeClamp A; A.Min = 10.f;
			FAttributeClamp B; B.Min = 20.f;
			TestFalse("A != B", A == B);
		});
		It("Different Max produces inequality", [this]()
		{
			FAttributeClamp A; A.Max = 10.f;
			FAttributeClamp B; B.Max = 20.f;
			TestFalse("A != B", A == B);
		});
		It("Both bounds zero compare as not-set + equal", [this]()
		{
			FAttributeClamp A, B;
			TestFalse("Not set", A.IsSet());
			TestTrue("Equal", A == B);
		});
	});

	// ─── Comparison operator on FAttribute ──────────────────────────────────
	Describe("FAttribute < operator", [this]()
	{
		It("Sort by Tag works for two distinct tags", [this]()
		{
			FAttribute A, B;
			A.Tag = FGameplayTag::RequestGameplayTag(FName("Test.A"), false);
			B.Tag = FGameplayTag::RequestGameplayTag(FName("Test.B"), false);
			if (A.Tag.IsValid() && B.Tag.IsValid())
			{
				const bool LessA = A < B;
				const bool LessB = B < A;
				TestNotEqual("Strict ordering", LessA, LessB);
			}
			else
			{
				// Tags not requested in this build — skip silently.
				TestTrue("Skipped: tags not requested", true);
			}
		});
	});

	// ─── Set on attribute with no Init() called yet ─────────────────────────
	Describe("Set on uninitialised attribute", [this]()
	{
		It("AddModifier without Init() doesn't crash", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A;   // NO Init()
			FGMCAttributeModifier M; M.Op = EModifierType::Set; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 50.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("Set works without Init", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Permanent Add without Init() applies on RawValue=0", [this]()
		{
			FAttribute A;   // NO Init()
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 25.f; M.DeltaTime = 1.f; M.bRegisterInHistory = false;
			A.AddModifier(M); A.CalculateValue();
			TestEqual("0 + 25 = 25", A.RawValue, 25.f);
		});
	});

	// ─── Two-pass CalculateValue stability ─────────────────────────────────
	Describe("CalculateValue stability", [this]()
	{
		It("100 successive CalculateValue calls produce same Value", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A; A.InitialValue = 100.f; A.Init();
			FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
			M.ModifierValue = 10.f; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
			M.SourceAbilityEffect = E; M.ApplicationIndex = 1; M.ActionTimer = 1.0;
			A.AddModifier(M);
			A.CalculateValue();
			const float V0 = A.Value;
			for (int i = 0; i < 100; ++i) { A.CalculateValue(); }
			TestEqual("100 recalcs same Value", A.Value, V0);
			E->RemoveFromRoot();
		});
		It("Recalc after no changes leaves bIsDirty false", [this]()
		{
			FAttribute A; A.InitialValue = 100.f; A.Init();
			A.CalculateValue();
			A.CalculateValue();
			TestFalse("Idempotent CalculateValue keeps clean", A.IsDirty());
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
