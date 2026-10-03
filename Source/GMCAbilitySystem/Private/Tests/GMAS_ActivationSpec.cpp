// Layer 2 tests: activation gating — ActivationRequiredTags, ActivationBlockedTags,
// BlockOtherAbility, BlockedByOtherAbility, and CancelAbilitiesWithTag.
//
// Two ability stubs (UGMAS_TestAbility = A, UGMAS_TestAbilityB = B) let us set up
// interactions between distinct ability classes.  All CDO tag containers are
// reset in TeardownHarness so tests never bleed state into each other.
//
// Activation flow reminder:
//   TryActivateAbility
//     → CheckActivationTags(CDO)          — returns false if Required/Blocked fail
//     → NewObject + CDO property copy
//     → Execute → PreBeginAbility
//         → IsOnCooldown                  → CancelAbility
//         → BlockedByOtherAbility loop    → CancelAbility
//         → IsAbilityTagBlocked           → CancelAbility
//         → BeginAbility
//             → CommitAbilityCooldown
//             → AbilityState = Initialized
//             → CancelConflictingAbilities (CancelAbilitiesWithTag)
//     → ActiveAbilities.Add(ID, Ability)  — always, even if Ended
//
// TryActivateAbility returns false ONLY for: null class, multi-instance guard,
// or CheckActivationTags failure.  All other cancellations return true while
// leaving the ability in Ended state (GetActiveAbilityCount == 0).

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestAbilityB.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASActivationSpec,
	"GMAS.Unit.Activation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;

	// GMAS.Test.Ability.Attack  — AbilityTag for class A (UGMAS_TestAbility)
	FGameplayTag AbilityTagA;
	// GMAS.Test.Ability.Defend  — AbilityTag for class B (UGMAS_TestAbilityB)
	FGameplayTag AbilityTagB;
	// GMAS.Test.Status.Burning  — used as a blocking / required status tag
	FGameplayTag BurningTag;

	void SetupHarness();
	void TeardownHarness();

END_DEFINE_SPEC(FGMASActivationSpec)

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

void FGMASActivationSpec::SetupHarness()
{
	static FNativeGameplayTag SAbilityTagA(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Ability.Attack"), TEXT("Attack ability tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SAbilityTagB(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Ability.Defend"), TEXT("Defend ability tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Burning"), TEXT("Burning status for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	AbilityTagA = SAbilityTagA.GetTag();
	AbilityTagB = SAbilityTagB.GetTag();
	BurningTag  = SBurningTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = -1.0;

	// Assign per-class ability tags up-front; tests override anything else they need.
	GetMutableDefault<UGMAS_TestAbility>()->AbilityTag          = AbilityTagA;
	GetMutableDefault<UGMAS_TestAbility>()->CooldownTime        = 0.f;
	GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;

	GetMutableDefault<UGMAS_TestAbilityB>()->AbilityTag         = AbilityTagB;
	GetMutableDefault<UGMAS_TestAbilityB>()->CooldownTime       = 0.f;
	GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = false;
}

void FGMASActivationSpec::TeardownHarness()
{
	// Fully reset both CDOs so no test state leaks.
	auto ResetCDO = [](UGMCAbility* CDO)
	{
		CDO->AbilityTag              = FGameplayTag();
		CDO->CooldownTime            = 0.f;
		CDO->bAllowMultipleInstances = false;
		CDO->ActivationRequiredTags  = FGameplayTagContainer();
		CDO->ActivationBlockedTags   = FGameplayTagContainer();
		CDO->BlockOtherAbility       = FGameplayTagContainer();
		CDO->BlockedByOtherAbility   = FGameplayTagContainer();
		CDO->CancelAbilitiesWithTag  = FGameplayTagContainer();
	};

	ResetCDO(GetMutableDefault<UGMAS_TestAbility>());
	ResetCDO(GetMutableDefault<UGMAS_TestAbilityB>());

	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AbilityComp = nullptr;
	MoveCmp     = nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASActivationSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── ActivationRequiredTags ────────────────────────────────────────────────
	// CheckActivationTags reads the CDO's ActivationRequiredTags against the
	// component's ActiveTags.  Returns false (blocks TryActivateAbility) when
	// the owner lacks a required tag.
	Describe("ActivationRequiredTags", [this]()
	{
		It("TryActivateAbility returns false when a required tag is absent", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationRequiredTags.AddTag(BurningTag);
			// Owner has no active tags → CheckActivationTags fails.
			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestFalse("activation blocked when required tag missing", bResult);
			TestEqual("no active instances", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
		});

		It("activation succeeds when the required tag is present on the owner", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationRequiredTags.AddTag(BurningTag);
			AbilityComp->AddActiveTag(BurningTag);
			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("activation succeeds with required tag present", bResult);
			TestEqual("one active instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
		});

		It("activation fails again after the required tag is removed", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationRequiredTags.AddTag(BurningTag);
			GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = true;

			AbilityComp->AddActiveTag(BurningTag);
			const bool bFirst = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("first activation succeeds", bFirst);

			// Remove the tag and end the first instance so the multi-instance guard is clear.
			AbilityComp->RemoveActiveTag(BurningTag);
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AbilityComp->GenPredictionTick(0.f); // cleanup Ended abilities

			const bool bSecond = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestFalse("activation blocked after required tag removed", bSecond);
		});
	});

	// ── ActivationBlockedTags ─────────────────────────────────────────────────
	// CheckActivationTags blocks activation when the owner has a tag listed in
	// ActivationBlockedTags.
	Describe("ActivationBlockedTags", [this]()
	{
		It("activation succeeds when the blocked tag is not active", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationBlockedTags.AddTag(BurningTag);
			// Owner has no BurningTag → no block.
			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("activation succeeds without the blocked tag", bResult);
			TestEqual("one active instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
		});

		It("TryActivateAbility returns false when a blocked tag is active on the owner", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationBlockedTags.AddTag(BurningTag);
			AbilityComp->AddActiveTag(BurningTag);
			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestFalse("activation blocked by owner tag", bResult);
			TestEqual("no active instances", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
		});

		It("activation succeeds again after the blocked tag is removed", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationBlockedTags.AddTag(BurningTag);
			AbilityComp->AddActiveTag(BurningTag);
			TestFalse("blocked while tag present", AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass()));

			AbilityComp->RemoveActiveTag(BurningTag);
			const bool bAfter = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("activation succeeds after blocked tag removed", bAfter);
		});
	});

	// ── BlockOtherAbility ─────────────────────────────────────────────────────
	// An active ability A with BlockOtherAbility containing AbilityTagB prevents
	// ability B from becoming active.  IsAbilityTagBlocked is checked inside
	// B's PreBeginAbility — TryActivateAbility still returns true, but the
	// instance ends up in Ended state (GetActiveAbilityCount == 0).
	Describe("BlockOtherAbility", [this]()
	{
		It("active ability A prevents ability B from becoming active via BlockOtherAbility", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->BlockOtherAbility.AddTag(AbilityTagB);
			GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = true;

			// Activate A — it goes to Initialized state and exposes its BlockOtherAbility.
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("A is active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			// Activate B — PreBeginAbility finds A's BlockOtherAbility matches B's AbilityTag → cancel.
			AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestEqual("B cancelled by A's BlockOtherAbility", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 0);
		});

		It("ability B activates successfully after ability A ends", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->BlockOtherAbility.AddTag(AbilityTagB);

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AbilityComp->GenPredictionTick(0.f); // remove Ended entry from map

			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestTrue("B activates once A has ended", bResult);
			TestEqual("B is active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
		});
	});

	// ── BlockedByOtherAbility ─────────────────────────────────────────────────
	// Ability B lists AbilityTagA in its BlockedByOtherAbility.  If an active
	// ability has AbilityTagA, B's PreBeginAbility cancels B.
	Describe("BlockedByOtherAbility", [this]()
	{
		It("ability B is cancelled when ability A (matching its BlockedByOtherAbility) is active", [this]()
		{
			GetMutableDefault<UGMAS_TestAbilityB>()->BlockedByOtherAbility.AddTag(AbilityTagA);
			GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = true;

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("A active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			// B's PreBeginAbility sees A's AbilityTagA matching BlockedByOtherAbility → cancel.
			AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestEqual("B cancelled by BlockedByOtherAbility", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 0);
		});

		It("ability B activates normally when no blocking ability is active", [this]()
		{
			GetMutableDefault<UGMAS_TestAbilityB>()->BlockedByOtherAbility.AddTag(AbilityTagA);
			// No A active — BlockedByOtherAbility loop finds nothing.
			const bool bResult = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestTrue("B activates with no blocker", bResult);
			TestEqual("B active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
		});
	});

	// ── CancelAbilitiesWithTag ────────────────────────────────────────────────
	// CancelConflictingAbilities is called after BeginAbility sets the state to
	// Initialized.  It ends any active ability whose AbilityTag matches a tag in
	// the activating ability's CancelAbilitiesWithTag.
	Describe("CancelAbilitiesWithTag", [this]()
	{
		It("activating B cancels a running instance of A via CancelAbilitiesWithTag", [this]()
		{
			GetMutableDefault<UGMAS_TestAbilityB>()->CancelAbilitiesWithTag.AddTag(AbilityTagA);
			GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = true;

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("A active before B activates", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());

			// B's CancelConflictingAbilities called EndAbilitiesByTag(AbilityTagA) → A ended.
			TestEqual("A cancelled after B activates", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
			TestEqual("B active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
		});

		It("abilities without the cancelled tag are unaffected", [this]()
		{
			// B has no CancelAbilitiesWithTag entries — nothing is cancelled.
			GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances  = true;
			GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = true;

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());

			TestEqual("A still active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()),  1);
			TestEqual("B active",       AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
		});

		It("CancelAbilitiesWithTag never cancels the activating ability itself", [this]()
		{
			// A includes its own AbilityTagA in CancelAbilitiesWithTag.
			// CancelConflictingAbilities skips self-cancellation.
			GetMutableDefault<UGMAS_TestAbility>()->CancelAbilitiesWithTag.AddTag(AbilityTagA);

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("A survives its own CancelAbilitiesWithTag",
				AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
