// Layer 2 tests: ability lifecycle, tag management, and cooldowns through
// UGMC_AbilitySystemComponent.
//
// Uses the same UGMAS_TestMovementCmp stub as GMAS_EffectSpec.  No world,
// no network stack — GenPredictionTick drives the component tick cycle.
//
// Ability CDO properties (AbilityTag, CooldownTime, bAllowMultipleInstances)
// are mutated in SetupHarness / TeardownHarness so every test starts from a
// known state.
//
// Limitations at this layer (covered in Layer 3):
//   • Ability queuing via BoundQueueV2 — requires a real GMC move loop.
//   • Networked ability confirmation (RPCConfirmAbilityActivation is skipped
//     because the stub has no owner → HasAuthority() = false).

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Ability/GMCAbility.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAbilitySpec,
	"GMAS.Unit.Ability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;

	// "GMAS.Test.Ability.Attack" — used as the test ability's AbilityTag.
	FGameplayTag AbilityTag;
	// "GMAS.Test.Status.Burning" — used for active-tag grant / check tests.
	FGameplayTag BurningTag;
	// "GMAS.Test.Status" — parent of BurningTag; used for hierarchical tag tests.
	FGameplayTag StatusTag;

	void SetupHarness();
	void TeardownHarness();

END_DEFINE_SPEC(FGMASAbilitySpec)

// ---------------------------------------------------------------------------
// Harness helpers
// ---------------------------------------------------------------------------

void FGMASAbilitySpec::SetupHarness()
{
	static FNativeGameplayTag SAbilityTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Ability.Attack"), TEXT("Attack ability for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Burning"), TEXT("Burning status for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SStatusTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status"), TEXT("Status parent tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	AbilityTag  = SAbilityTag.GetTag();
	BurningTag  = SBurningTag.GetTag();
	StatusTag   = SStatusTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AbilityComp->GMCMovementComponent = MoveCmp;

	// No AttributeDataAssets needed — InstantiateAttributes() returns early when empty.
	AbilityComp->BindReplicationData();

	// Seed ActionTimer to -1.0 so GenerateAbilityID() and GetNextAvailableEffectID()
	// produce non-zero IDs before the first GenPredictionTick.
	AbilityComp->ActionTimer = -1.0;

	// Configure the test ability CDO so all tests share the same AbilityTag.
	// CooldownTime and bAllowMultipleInstances are reset in TeardownHarness.
	GetMutableDefault<UGMAS_TestAbility>()->AbilityTag           = AbilityTag;
	GetMutableDefault<UGMAS_TestAbility>()->CooldownTime         = 0.f;
	GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;
}

void FGMASAbilitySpec::TeardownHarness()
{
	// Restore CDO to neutral defaults so tests don't bleed into each other.
	GetMutableDefault<UGMAS_TestAbility>()->AbilityTag              = FGameplayTag();
	GetMutableDefault<UGMAS_TestAbility>()->CooldownTime            = 0.f;
	GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;

	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AbilityComp = nullptr;
	MoveCmp     = nullptr;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASAbilitySpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// ── Active Tags ───────────────────────────────────────────────────────────
	Describe("Active tags", [this]()
	{
		It("AddActiveTag makes HasActiveTag return true", [this]()
		{
			AbilityComp->AddActiveTag(BurningTag);
			TestTrue("Burning active after add", AbilityComp->HasActiveTag(BurningTag));
		});

		It("RemoveActiveTag makes HasActiveTag return false", [this]()
		{
			AbilityComp->AddActiveTag(BurningTag);
			AbilityComp->RemoveActiveTag(BurningTag);
			TestFalse("Burning removed after remove", AbilityComp->HasActiveTag(BurningTag));
		});

		It("HasActiveTag matches a child tag via hierarchy; HasActiveTagExact does not", [this]()
		{
			// Add the leaf tag "GMAS.Test.Status.Burning".
			// HasActiveTag("GMAS.Test.Status") uses hierarchical matching → true.
			// HasActiveTagExact("GMAS.Test.Status") requires an exact entry → false.
			AbilityComp->AddActiveTag(BurningTag);
			TestTrue ("HasActiveTag(parent) = true  (hierarchical)",
				AbilityComp->HasActiveTag(StatusTag));
			TestFalse("HasActiveTagExact(parent) = false (exact only)",
				AbilityComp->HasActiveTagExact(StatusTag));
		});
	});

	// ── Granted Abilities ─────────────────────────────────────────────────────
	Describe("Granted abilities", [this]()
	{
		It("GrantAbilityByTag makes HasGrantedAbilityTag return true", [this]()
		{
			AbilityComp->GrantAbilityByTag(AbilityTag);
			TestTrue("ability tag granted", AbilityComp->HasGrantedAbilityTag(AbilityTag));
		});

		It("RemoveGrantedAbilityByTag makes HasGrantedAbilityTag return false", [this]()
		{
			AbilityComp->GrantAbilityByTag(AbilityTag);
			AbilityComp->RemoveGrantedAbilityByTag(AbilityTag);
			TestFalse("ability tag revoked", AbilityComp->HasGrantedAbilityTag(AbilityTag));
		});

		It("granting the same tag twice is idempotent", [this]()
		{
			AbilityComp->GrantAbilityByTag(AbilityTag);
			AbilityComp->GrantAbilityByTag(AbilityTag); // second call must not crash or duplicate
			TestTrue("still granted after double-grant", AbilityComp->HasGrantedAbilityTag(AbilityTag));
		});
	});

	// ── Ability Lifecycle ─────────────────────────────────────────────────────
	Describe("Ability lifecycle", [this]()
	{
		It("TryActivateAbility returns false for a null class", [this]()
		{
			TestFalse("null class activation fails", AbilityComp->TryActivateAbility(nullptr));
		});

		It("successful activation: GetActiveAbilityCount increases and ability is Initialized", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());

			TestEqual("one active instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			// Retrieve the (single) active ability instance and check its state.
			TArray<UGMCAbility*> Abilities;
			AbilityComp->GetActiveAbilities().GenerateValueArray(Abilities);
			const bool bInitialized = Abilities.ContainsByPredicate([](const UGMCAbility* A)
			{
				return A && A->AbilityState == EAbilityState::Initialized;
			});
			TestTrue("ability state is Initialized", bInitialized);
		});

		It("EndAbility sets state Ended; GenPredictionTick removes it from ActiveAbilities", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("ability active before end", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			// End the ability through the component's public API.
			AbilityComp->EndAbilitiesByTag(AbilityTag);
			// Active count is already 0 — state is Ended.
			TestEqual("count drops to 0 after EndAbility",
				AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);

			// GenPredictionTick → CleanupStaleAbilities removes the Ended entry.
			AbilityComp->GenPredictionTick(0.f);
			TestTrue("ActiveAbilities map is empty after cleanup",
				AbilityComp->GetActiveAbilities().IsEmpty());
		});

		It("bAllowMultipleInstances=false blocks a second activation while the first is running", [this]()
		{
			// CDO already has bAllowMultipleInstances=false (set in SetupHarness).
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestEqual("first activation succeeds", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			const bool bSecond = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestFalse("second activation rejected", bSecond);
			TestEqual("still only one active instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
		});
	});

	// ── Cooldown ──────────────────────────────────────────────────────────────
	Describe("Cooldown", [this]()
	{
		It("GetCooldownForAbility returns 0 for an unknown tag", [this]()
		{
			TestEqual("unknown tag → 0", AbilityComp->GetCooldownForAbility(AbilityTag), 0.f);
		});

		It("SetCooldownForAbility / GetCooldownForAbility round-trip", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 5.f);
			TestEqual("cooldown stored as 5", AbilityComp->GetCooldownForAbility(AbilityTag), 5.f);
		});

		It("GenAncillaryTick decrements the cooldown by DeltaTime", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 5.f);
			AbilityComp->GenAncillaryTick(3.f, false); // TickActiveCooldowns subtracts 3
			TestNearlyEqual("5 - 3 = 2 remaining",
				AbilityComp->GetCooldownForAbility(AbilityTag), 2.f, KINDA_SMALL_NUMBER);
		});

		It("cooldown expires and is removed after sufficient DeltaTime", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 2.f);
			AbilityComp->GenAncillaryTick(3.f, false); // exceeds cooldown → entry removed
			TestEqual("expired cooldown returns 0",
				AbilityComp->GetCooldownForAbility(AbilityTag), 0.f);
		});

		It("bApplyCooldownAtAbilityBegin=true blocks re-activation while cooldown runs", [this]()
		{
			// Configure the CDO with a cooldown so the first BeginAbility() commits it.
			GetMutableDefault<UGMAS_TestAbility>()->CooldownTime            = 5.f;
			GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = true; // only cooldown blocks

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			// BeginAbility → CommitAbilityCooldown → SetCooldownForAbility(AbilityTag, 5)
			TestTrue("cooldown registered after activation",
				AbilityComp->GetCooldownForAbility(AbilityTag) > 0.f);

			// Second activation: Execute → PreBeginAbility → IsOnCooldown → CancelAbility.
			// The ability ends immediately; GetActiveAbilityCount excludes Ended instances.
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			// One non-Ended instance (the first), one Ended (the rejected second).
			TestEqual("only the first instance is non-ended",
				AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);

			// After cooldown expires, a fresh activation succeeds normally.
			AbilityComp->EndAbilitiesByTag(AbilityTag); // end the running first ability
			AbilityComp->GenAncillaryTick(6.f, false); // tick cooldown to 0
			AbilityComp->GenPredictionTick(0.f);       // cleanup stale Ended abilities
			TestEqual("cooldown expired",
				AbilityComp->GetCooldownForAbility(AbilityTag), 0.f);
			const bool bAfterExpiry = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("activation succeeds after cooldown expires", bAfterExpiry);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
