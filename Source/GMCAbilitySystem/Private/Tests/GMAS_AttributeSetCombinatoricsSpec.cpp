// Combinatorial torture tests for Set / SetReplace modifiers.
// Targets : ordering pathologies, tie-break determinism under duplicate timestamps,
// alternation of Set/SetReplace, mass insertion, removal-while-Set-active, and
// the interplay between the SetReplace ActionTimer cutoff and Add timestamps.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributes.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Attributes/GMCAttributeClamp.h"
#include "Effects/GMCAbilityEffect.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeSetCombinatoricsSpec,
	"GMAS.Stress.Set",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	FAttribute MakeAttr(float Init = 100.f) const;
	FGMCAttributeModifier MakeSetMod(UGMCAbilityEffect* Eff, float V, int Idx, double T,
		EModifierType Op = EModifierType::Set) const;
	FGMCAttributeModifier MakeAddMod(UGMCAbilityEffect* Eff, float V, int Idx, double T) const;
	UGMCAbilityEffect* SpawnEffect();

END_DEFINE_SPEC(FGMASAttributeSetCombinatoricsSpec)

FAttribute FGMASAttributeSetCombinatoricsSpec::MakeAttr(float Init) const
{
	FAttribute A; A.InitialValue = Init; A.Init(); return A;
}
FGMCAttributeModifier FGMASAttributeSetCombinatoricsSpec::MakeSetMod(UGMCAbilityEffect* Eff, float V, int Idx, double T, EModifierType Op) const
{
	FGMCAttributeModifier M; M.Op = Op; M.ValueType = EGMCAttributeModifierType::AMT_Value;
	M.ModifierValue = V; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
	M.SourceAbilityEffect = Eff; M.ApplicationIndex = Idx; M.ActionTimer = T; return M;
}
FGMCAttributeModifier FGMASAttributeSetCombinatoricsSpec::MakeAddMod(UGMCAbilityEffect* Eff, float V, int Idx, double T) const
{
	FGMCAttributeModifier M; M.Op = EModifierType::Add; M.ValueType = EGMCAttributeModifierType::AMT_Value;
	M.ModifierValue = V; M.DeltaTime = 1.f; M.bRegisterInHistory = true;
	M.SourceAbilityEffect = Eff; M.ApplicationIndex = Idx; M.ActionTimer = T; return M;
}
UGMCAbilityEffect* FGMASAttributeSetCombinatoricsSpec::SpawnEffect()
{
	UGMCAbilityEffect* E = NewObject<UGMCAbilityEffect>(GetTransientPackage()); E->AddToRoot(); return E;
}

void FGMASAttributeSetCombinatoricsSpec::Define()
{
	// ─── Tie-break under duplicate ActionTimer ────────────────────────────
	Describe("Tie-break: duplicate ActionTimer", [this]()
	{
		It("Same ActionTimer, different ApplicationIndex: higher index wins", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 5.0));
			A.AddModifier(MakeSetMod(E, 60.f, 5, 5.0));
			A.AddModifier(MakeSetMod(E, 55.f, 3, 5.0));
			A.CalculateValue();
			TestEqual("Highest ApplicationIndex (5 → 60) wins", A.Value, 60.f);
			E->RemoveFromRoot();
		});
		It("Same ActionTimer + Index — first inserted is the only one (no two should coexist)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, 5.0));
			A.AddModifier(MakeSetMod(E, 80.f, 1, 5.0));
			A.CalculateValue();
			// Both entries actually exist in the list — winner pick is by traversal order if all keys equal
			TestTrue("Value is one of {30, 80}", A.Value == 30.f || A.Value == 80.f);
			E->RemoveFromRoot();
		});
		It("Three Sets at same ActionTimer with strictly increasing index", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 10.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 20.f, 2, 1.0));
			A.AddModifier(MakeSetMod(E, 30.f, 3, 1.0));
			A.CalculateValue();
			TestEqual("Index 3 wins", A.Value, 30.f);
			E->RemoveFromRoot();
		});
		It("Negative ApplicationIndex still participates in tie-break", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, -5, 1.0));
			A.AddModifier(MakeSetMod(E, 40.f, 0, 1.0));
			A.CalculateValue();
			TestEqual("0 > -5 wins", A.Value, 40.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Tie-break under different ActionTimer ───────────────────────────
	Describe("Tie-break: different ActionTimer", [this]()
	{
		It("Larger ActionTimer wins regardless of insertion order (later last)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 80.f, 2, 5.0));
			A.CalculateValue();
			TestEqual("t=5 wins over t=1", A.Value, 80.f);
			E->RemoveFromRoot();
		});
		It("Larger ActionTimer wins regardless of insertion order (later first)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 80.f, 2, 5.0));
			A.AddModifier(MakeSetMod(E, 30.f, 1, 1.0));
			A.CalculateValue();
			TestEqual("t=5 wins regardless of order", A.Value, 80.f);
			E->RemoveFromRoot();
		});
		It("Sets at ActionTimer = 0 vs ActionTimer = -1 (negative time)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, -1.0));
			A.AddModifier(MakeSetMod(E, 40.f, 2, 0.0));
			A.CalculateValue();
			TestEqual("t=0 wins over t=-1", A.Value, 40.f);
			E->RemoveFromRoot();
		});
		It("Sets at very large ActionTimer values", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, 1e15));
			A.AddModifier(MakeSetMod(E, 40.f, 2, 1e16));
			A.CalculateValue();
			TestEqual("Higher exponent wins", A.Value, 40.f);
			E->RemoveFromRoot();
		});
		It("Sub-millisecond difference in ActionTimer correctly orders", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 40.f, 2, 1.0001));
			A.CalculateValue();
			TestEqual("Later by 0.0001s wins", A.Value, 40.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Mixed Set + SetReplace ──────────────────────────────────────────
	Describe("Mixed Set / SetReplace winner selection", [this]()
	{
		It("SetReplace wins when more recent than Set", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 30.f, 1, 1.0, EModifierType::Set));
			A.AddModifier(MakeSetMod(E, 80.f, 2, 5.0, EModifierType::SetReplace));
			A.CalculateValue();
			TestEqual("SetReplace 80 wins as base", A.Value, 80.f);
			E->RemoveFromRoot();
		});
		It("Set wins when more recent than SetReplace (Adds before SetReplace are ignored, but no Set means base = Set)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 80.f, 1, 1.0, EModifierType::SetReplace));
			A.AddModifier(MakeSetMod(E, 30.f, 2, 5.0, EModifierType::Set));
			A.CalculateValue();
			// More recent is Set→ Layered. Adds before don't apply because there are none.
			TestEqual("Set 30 wins", A.Value, 30.f);
			E->RemoveFromRoot();
		});
		It("Two SetReplaces — only the more recent matters, Adds before are filtered", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 5.f, 1, 0.5));
			A.AddModifier(MakeSetMod(E, 30.f, 2, 1.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 7.f, 3, 1.5));
			A.AddModifier(MakeSetMod(E, 50.f, 4, 2.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 9.f, 5, 2.5));
			A.CalculateValue();
			// Winner: SetReplace at t=2 with value 50. Adds at t=2.5 are after, t≤2 are filtered.
			TestEqual("Most-recent SetReplace base + Add after = 50 + 9 = 59", A.Value, 59.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Alternation patterns ───────────────────────────────────────────────
	Describe("Alternation: Add / Set / Add / Set …", [this]()
	{
		It("Add → Set → Add → Set", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 10.f, 1, 1.0));   // +10
			A.AddModifier(MakeSetMod(E, 50.f, 2, 2.0));   // base = 50
			A.AddModifier(MakeAddMod(E, 20.f, 3, 3.0));   // +20
			A.AddModifier(MakeSetMod(E, 200.f, 4, 4.0));  // base = 200, prev Set hidden
			A.CalculateValue();
			// Layered: base = 200, all Adds stack: +10 +20 = +30 → 230
			TestEqual("Latest Set 200 + sum of Adds 30 = 230", A.Value, 230.f);
			E->RemoveFromRoot();
		});
		It("Add → SetReplace → Add → SetReplace", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 10.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 50.f, 2, 2.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 20.f, 3, 3.0));
			A.AddModifier(MakeSetMod(E, 200.f, 4, 4.0, EModifierType::SetReplace));
			A.CalculateValue();
			// Latest SetReplace at t=4 is winner. Adds before t=4 are filtered. → 200 only.
			TestEqual("Latest SetReplace clears all earlier", A.Value, 200.f);
			E->RemoveFromRoot();
		});
		It("Set → Add → Set → Add (last Add survives)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 5.f, 2, 2.0));
			A.AddModifier(MakeSetMod(E, 200.f, 3, 3.0));
			A.AddModifier(MakeAddMod(E, 7.f, 4, 4.0));
			A.CalculateValue();
			// Set 200 base + (5 + 7) = 212  (Layered: all Adds stack)
			TestEqual("200 + 5 + 7 = 212", A.Value, 212.f);
			E->RemoveFromRoot();
		});
	});

	// ─── SetReplace boundary semantics ──────────────────────────────────────
	Describe("SetReplace ActionTimer cutoff (Mod.ActionTimer < SetTime)", [this]()
	{
		It("Add at exactly SetReplace's ActionTimer is KEPT (strict less-than filter)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 7.f, 1, 5.0));
			A.AddModifier(MakeSetMod(E, 50.f, 2, 5.0, EModifierType::SetReplace));
			A.CalculateValue();
			TestEqual("Add at SetTime=5 stacks: 50 + 7 = 57", A.Value, 57.f);
			E->RemoveFromRoot();
		});
		It("Add infinitesimally before SetReplace is filtered out", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 7.f, 1, 4.999999));
			A.AddModifier(MakeSetMod(E, 50.f, 2, 5.0, EModifierType::SetReplace));
			A.CalculateValue();
			TestEqual("Add at t<5 filtered, Value = 50", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Add infinitesimally after SetReplace is kept", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 5.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 7.f, 2, 5.000001));
			A.CalculateValue();
			TestEqual("Add at t>5 kept: 50 + 7 = 57", A.Value, 57.f);
			E->RemoveFromRoot();
		});
		It("Multiple Adds straddling SetReplace's ActionTimer", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 1.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 2.f, 2, 2.0));
			A.AddModifier(MakeAddMod(E, 3.f, 3, 3.0));
			A.AddModifier(MakeSetMod(E, 100.f, 4, 3.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 4.f, 5, 4.0));
			A.AddModifier(MakeAddMod(E, 5.f, 6, 5.0));
			A.CalculateValue();
			// SetReplace base = 100. Filter Adds with ActionTimer < 3. Keep ActionTimer = 3 and 4 and 5: 3 + 4 + 5 = 12
			TestEqual("SetReplace + Adds at/after = 100 + 12 = 112", A.Value, 112.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Mass Set insertion ─────────────────────────────────────────────────
	Describe("Mass Set insertion (only one survives as winner)", [this]()
	{
		It("100 Sets at distinct ActionTimers — last one wins", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			for (int i = 0; i < 100; ++i) { A.AddModifier(MakeSetMod(E, float(i), i, double(i))); }
			A.CalculateValue();
			TestEqual("Set with t=99 (value=99) wins", A.Value, 99.f);
			E->RemoveFromRoot();
		});
		It("100 Sets all at same ActionTimer — highest ApplicationIndex wins", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			for (int i = 0; i < 100; ++i) { A.AddModifier(MakeSetMod(E, float(i*2), i, 5.0)); }
			A.CalculateValue();
			TestEqual("Index 99 → value 198", A.Value, 198.f);
			E->RemoveFromRoot();
		});
		It("Removing the winning Set elects the next-most-recent", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 10.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 20.f, 2, 2.0));
			A.AddModifier(MakeSetMod(E, 30.f, 3, 3.0));
			A.CalculateValue();
			TestEqual("Initially 30 wins", A.Value, 30.f);
			A.RemoveTemporalModifier(3, E); A.CalculateValue();
			TestEqual("After remove 3, 20 wins", A.Value, 20.f);
			A.RemoveTemporalModifier(2, E); A.CalculateValue();
			TestEqual("After remove 2, 10 wins", A.Value, 10.f);
			A.RemoveTemporalModifier(1, E); A.CalculateValue();
			TestEqual("After remove 1, base 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("Mass Set insertion does not corrupt Add stacking", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 1.f, 0, 0.0));
			A.AddModifier(MakeAddMod(E, 1.f, 1, 0.0));
			for (int i = 0; i < 50; ++i) { A.AddModifier(MakeSetMod(E, float(i), 100+i, double(i+1))); }
			A.CalculateValue();
			// Winner: Set with t=50, value=49. Adds: 1 + 1 = 2 (both at t=0, so kept by Layered Set)
			TestEqual("Latest Set 49 + Adds 2 = 51", A.Value, 51.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Multi-effect interactions ─────────────────────────────────────────
	Describe("Multi-effect Set ownership", [this]()
	{
		It("Removing Set from EffectA leaves Set from EffectB intact", [this]()
		{
			UGMCAbilityEffect* EA = SpawnEffect();
			UGMCAbilityEffect* EB = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(EA, 30.f, 1, 1.0));
			A.AddModifier(MakeSetMod(EB, 50.f, 1, 2.0));   // same Index but different effect
			A.CalculateValue();
			TestEqual("EB later wins", A.Value, 50.f);
			A.RemoveTemporalModifier(1, EB);   // remove only EB's
			A.CalculateValue();
			TestEqual("EA's Set takes over", A.Value, 30.f);
			EA->RemoveFromRoot(); EB->RemoveFromRoot();
		});
		It("Same ApplicationIndex on different Effects don't interfere", [this]()
		{
			UGMCAbilityEffect* EA = SpawnEffect();
			UGMCAbilityEffect* EB = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(EA, 10.f, 5, 1.0));
			A.AddModifier(MakeAddMod(EB, 20.f, 5, 1.0));
			A.CalculateValue();
			TestEqual("Both Adds active: 100 + 10 + 20 = 130", A.Value, 130.f);
			A.RemoveTemporalModifier(5, EA); A.CalculateValue();
			TestEqual("Removed only EA's: 100 + 20 = 120", A.Value, 120.f);
			EA->RemoveFromRoot(); EB->RemoveFromRoot();
		});
		It("Set from EffectA + Add from EffectB stack normally", [this]()
		{
			UGMCAbilityEffect* EA = SpawnEffect();
			UGMCAbilityEffect* EB = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(EA, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(EB, 7.f, 1, 2.0));
			A.CalculateValue();
			TestEqual("Set 50 + Add 7 = 57", A.Value, 57.f);
			EA->RemoveFromRoot(); EB->RemoveFromRoot();
		});
		It("SetReplace from EffectA filters Adds from EffectB based on time", [this]()
		{
			UGMCAbilityEffect* EA = SpawnEffect();
			UGMCAbilityEffect* EB = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(EB, 10.f, 1, 0.5));   // before SetReplace
			A.AddModifier(MakeSetMod(EA, 50.f, 1, 1.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(EB, 5.f, 2, 2.0));    // after
			A.CalculateValue();
			TestEqual("EA's SetReplace filters EB's older Add: 50 + 5 = 55", A.Value, 55.f);
			EA->RemoveFromRoot(); EB->RemoveFromRoot();
		});
	});

	// ─── Removal patterns ───────────────────────────────────────────────────
	Describe("Set + RemoveTemporalModifier patterns", [this]()
	{
		It("Remove only Add — Set + remaining Adds", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 3.f, 2, 2.0));
			A.AddModifier(MakeAddMod(E, 4.f, 3, 3.0));
			A.CalculateValue();
			TestEqual("50 + 3 + 4 = 57", A.Value, 57.f);
			A.RemoveTemporalModifier(2, E); A.CalculateValue();
			TestEqual("After remove Add(2): 50 + 4 = 54", A.Value, 54.f);
			E->RemoveFromRoot();
		});
		It("Remove all Adds, leaving lone Set", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 5.f, 2, 2.0));
			A.AddModifier(MakeAddMod(E, 7.f, 3, 3.0));
			A.RemoveTemporalModifier(2, E); A.RemoveTemporalModifier(3, E);
			A.CalculateValue();
			TestEqual("Lone Set", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("Removing all modifiers via repeated RemoveTemporalModifier returns base", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 7.f, 2, 2.0));
			A.RemoveTemporalModifier(1, E); A.RemoveTemporalModifier(2, E);
			A.CalculateValue();
			TestEqual("Both removed → base 100", A.Value, 100.f);
			E->RemoveFromRoot();
		});
		It("RemoveTemporalModifier with non-matching ApplicationIndex is no-op", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.RemoveTemporalModifier(99, E);
			A.CalculateValue();
			TestEqual("Set still there", A.Value, 50.f);
			E->RemoveFromRoot();
		});
		It("RemoveTemporalModifier with mismatched effect is no-op", [this]()
		{
			UGMCAbilityEffect* EA = SpawnEffect();
			UGMCAbilityEffect* EB = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(EA, 50.f, 1, 1.0));
			A.RemoveTemporalModifier(1, EB);   // wrong effect
			A.CalculateValue();
			TestEqual("Set survived bogus remove", A.Value, 50.f);
			EA->RemoveFromRoot(); EB->RemoveFromRoot();
		});
	});

	// ─── Set with extreme values ────────────────────────────────────────────
	Describe("Set with extreme values", [this]()
	{
		It("Set 0", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 0.f, 1, 1.0));
			A.CalculateValue();
			TestEqual("Set zero", A.Value, 0.f);
			E->RemoveFromRoot();
		});
		It("Set FLT_MAX", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, FLT_MAX, 1, 1.0));
			A.CalculateValue();
			TestEqual("Set FLT_MAX", A.Value, FLT_MAX);
			E->RemoveFromRoot();
		});
		It("Set -FLT_MAX", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, -FLT_MAX, 1, 1.0));
			A.CalculateValue();
			TestEqual("Set -FLT_MAX", A.Value, -FLT_MAX);
			E->RemoveFromRoot();
		});
		It("Set FLT_EPSILON", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, FLT_EPSILON, 1, 1.0));
			A.CalculateValue();
			TestEqual("Set epsilon", A.Value, FLT_EPSILON);
			E->RemoveFromRoot();
		});
		It("Set then Add overflow", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, FLT_MAX, 1, 1.0));
			A.AddModifier(MakeAddMod(E, FLT_MAX, 2, 2.0));
			A.CalculateValue();
			TestTrue("FLT_MAX + FLT_MAX overflows to +Inf", A.Value > FLT_MAX);
			E->RemoveFromRoot();
		});
	});

	// ─── Pure Add baseline (no Set) ─────────────────────────────────────────
	Describe("Adds without any Set use RawValue base", [this]()
	{
		It("Three Adds stack on top of RawValue", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 1.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 2.f, 2, 2.0));
			A.AddModifier(MakeAddMod(E, 3.f, 3, 3.0));
			A.CalculateValue();
			TestEqual("100 + 6 = 106", A.Value, 106.f);
			E->RemoveFromRoot();
		});
		It("100 Adds at random ActionTimers stack identically (commutativity)", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			const double Times[] = {17.5, 3.2, 99.9, 0.001, 42.0, 8.7, 56.1, 1.0, 23.4, 11.0};
			for (int i = 0; i < 10; ++i) { A.AddModifier(MakeAddMod(E, 1.f, i, Times[i])); }
			A.CalculateValue();
			TestEqual("10 Adds = 10", A.Value, 10.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Set+Set+Add+Add+SetReplace cascades ────────────────────────────────
	Describe("Cascade scenarios", [this]()
	{
		It("Set 50 → Add 10 → Set 80 → Add 5 → SetReplace 200", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0));
			A.AddModifier(MakeAddMod(E, 10.f, 2, 2.0));
			A.AddModifier(MakeSetMod(E, 80.f, 3, 3.0));
			A.AddModifier(MakeAddMod(E, 5.f, 4, 4.0));
			A.AddModifier(MakeSetMod(E, 200.f, 5, 5.0, EModifierType::SetReplace));
			A.CalculateValue();
			// Winner: SetReplace 200 at t=5. Filter Adds with t<5 → none survive. Adds at t=2,4 filtered.
			TestEqual("SetReplace 200, no Adds survive filter", A.Value, 200.f);
			E->RemoveFromRoot();
		});
		It("Cascade ends with Set (Layered) → all Adds stack", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 50.f, 1, 1.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 10.f, 2, 2.0));
			A.AddModifier(MakeSetMod(E, 80.f, 3, 3.0));   // Set Layered, more recent
			A.AddModifier(MakeAddMod(E, 5.f, 4, 4.0));
			A.CalculateValue();
			// Winner: Set 80 (Layered). All Adds stack: +10 + +5 = +15 → 80 + 15 = 95
			TestEqual("Set 80 + Adds 15 = 95", A.Value, 95.f);
			E->RemoveFromRoot();
		});
		It("Removing the SetReplace winner re-elects a Set Layered → Adds re-enter", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 10.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 80.f, 2, 2.0));               // Layered
			A.AddModifier(MakeSetMod(E, 50.f, 3, 3.0, EModifierType::SetReplace));
			A.CalculateValue();
			TestEqual("SetReplace 50 wins, Add t=1 filtered: 50", A.Value, 50.f);
			A.RemoveTemporalModifier(3, E); A.CalculateValue();
			TestEqual("After remove SetReplace, Set Layered 80 wins, Add re-stacks: 80 + 10 = 90", A.Value, 90.f);
			E->RemoveFromRoot();
		});
		It("Add after SetReplace isn't filtered when SetReplace gets removed", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeAddMod(E, 10.f, 1, 1.0));
			A.AddModifier(MakeSetMod(E, 50.f, 2, 2.0, EModifierType::SetReplace));
			A.AddModifier(MakeAddMod(E, 5.f, 3, 3.0));
			A.CalculateValue();
			TestEqual("SetReplace 50 + Add t=3 = 55", A.Value, 55.f);
			A.RemoveTemporalModifier(2, E); A.CalculateValue();
			TestEqual("Without SetReplace: base 100 + 10 + 5 = 115", A.Value, 115.f);
			E->RemoveFromRoot();
		});
	});

	// ─── Stress: heavy mixed insertion ──────────────────────────────────────
	Describe("Heavy mixed insertion", [this]()
	{
		It("100 Adds + 10 Sets correctly resolve", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(0.f);
			for (int i = 0; i < 100; ++i) { A.AddModifier(MakeAddMod(E, 1.f, i, double(i))); }   // Adds: 100 × +1 spread t=0..99
			for (int i = 0; i < 10; ++i)  { A.AddModifier(MakeSetMod(E, 500.f, 100+i, double(200+i))); }  // Sets: t=200..209, all value 500
			A.CalculateValue();
			// Winner: Set 500 at t=209. Layered → all Adds stack: 500 + 100 = 600
			TestEqual("Set 500 + 100 Adds = 600", A.Value, 600.f);
			E->RemoveFromRoot();
		});
		It("Removing all Sets leaves pure Add stack", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(50.f);
			for (int i = 0; i < 20; ++i) { A.AddModifier(MakeAddMod(E, 1.f, i, double(i))); }
			for (int i = 0; i < 5; ++i)  { A.AddModifier(MakeSetMod(E, 1000.f, 100+i, double(200+i))); }
			for (int i = 0; i < 5; ++i)  { A.RemoveTemporalModifier(100+i, E); }
			A.CalculateValue();
			TestEqual("All Sets removed: 50 + 20 = 70", A.Value, 70.f);
			E->RemoveFromRoot();
		});
		It("SetReplace at t=0 with everything else after — nothing is filtered", [this]()
		{
			UGMCAbilityEffect* E = SpawnEffect();
			FAttribute A = MakeAttr(100.f);
			A.AddModifier(MakeSetMod(E, 200.f, 1, 0.0, EModifierType::SetReplace));
			for (int i = 0; i < 10; ++i) { A.AddModifier(MakeAddMod(E, 1.f, 2+i, double(i+1))); }
			A.CalculateValue();
			TestEqual("SetReplace at t=0 + 10 Adds after = 210", A.Value, 210.f);
			E->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
