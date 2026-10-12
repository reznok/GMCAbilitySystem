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
#include <limits>
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
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

	Describe("Spawn clock readiness", [this]()
	{
		It("early held input waits without allocating an operation and drains once", [this]()
		{
			UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!TestNotNull("startup world", World)) return;
			ON_SCOPE_EXIT { World->DestroyWorld(false); };
			AActor* Owner = World->SpawnActor<AActor>();
			if (!TestNotNull("startup owner", Owner)) return;
			AbilityComp->RemoveFromRoot();
			AbilityComp = NewObject<UGMC_AbilitySystemComponent>(Owner);
			AbilityComp->AddToRoot();
			AbilityComp->GMCMovementComponent = MoveCmp;
			AbilityComp->BindReplicationData();
			AbilityComp->UpdateStartupPredictionClockForTest(0.0);
			AbilityComp->QueueAbility(AbilityTag, nullptr, true);
			AbilityComp->QueueAbility(AbilityTag, nullptr, true);
			TestEqual("held input coalesced", AbilityComp->GetStartupAbilityRequestCountForTest(), 1);
			TestTrue("no payload allocated before the clock is ready", AbilityComp->GetBoundQueueV2ForTest().GetPayloadCount() == 0);
			TestFalse("direct activation safely refused", AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
			TestEqual("no live ability can hold the gate", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
			AbilityComp->UpdateStartupPredictionClockForTest(10.0);
			TestEqual("startup requests drained", AbilityComp->GetStartupAbilityRequestCountForTest(), 0);
			TestEqual("one operation allocated", AbilityComp->GetBoundQueueV2ForTest().GetPayloadCount(), 1);
			AbilityComp->UpdateStartupPredictionClockForTest(11.0);
			TestEqual("no second dispatch", AbilityComp->GetBoundQueueV2ForTest().GetPayloadCount(), 1);
			TestTrue("activation works after initialization", AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
		});
		It("non-finite and negative spawn clocks cannot activate an ability", [this]()
		{
			UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
			if (!TestNotNull("startup world", World)) return;
			ON_SCOPE_EXIT { World->DestroyWorld(false); };
			AActor* Owner = World->SpawnActor<AActor>();
			if (!TestNotNull("startup owner", Owner)) return;
			AbilityComp->RemoveFromRoot();
			AbilityComp = NewObject<UGMC_AbilitySystemComponent>(Owner);
			AbilityComp->AddToRoot();
			AbilityComp->GMCMovementComponent = MoveCmp;
			AbilityComp->BindReplicationData();
			for (double Timestamp : { -1.0, std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity() })
			{
				AbilityComp->UpdateStartupPredictionClockForTest(Timestamp);
				TestFalse("invalid clock refused", AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
			}
			AbilityComp->UpdateStartupPredictionClockForTest(2.0);
			TestTrue("valid clock releases the gate", AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass()));
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

		// ⚠ Cooldowns are ABSOLUTE EXPIRY TIMES in ActionTimer units, not remaining durations.
		// SetCooldownForAbility stores ActionTimer + Duration; GetCooldownForAbility returns
		// Expiry - ActionTimer floored at 0; TickActiveCooldowns is a garbage-collection pass that
		// deliberately ignores its DeltaTime (GMC re-executes combined moves, and a
		// decrement-per-tick model drifted 2-2.6x faster than wall clock on a laggy client).
		//
		// So time is advanced by moving ActionTimer, NOT by passing DeltaTime to the tick.
		It("replayed ancillary ticks at the same prediction time do not shorten cooldown", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 5.f);
			AbilityComp->GenAncillaryTick(3.f, false);
			AbilityComp->GenAncillaryTick(3.f, false);
			TestEqual("unchanged prediction time keeps all five seconds", AbilityComp->GetCooldownForAbility(AbilityTag), 5.f);
			AbilityComp->ActionTimer += 3.0;
			AbilityComp->GenAncillaryTick(3.f, false);
			AbilityComp->GenAncillaryTick(3.f, false);
			TestNearlyEqual("advancing three seconds consumes time exactly once", AbilityComp->GetCooldownForAbility(AbilityTag), 2.f, KINDA_SMALL_NUMBER);
		});
		It("the remaining cooldown falls as ActionTimer advances", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 5.f); // expiry = ActionTimer + 5
			AbilityComp->ActionTimer += 3.0;
			TestNearlyEqual("5 - 3 = 2 remaining",
				AbilityComp->GetCooldownForAbility(AbilityTag), 2.f, KINDA_SMALL_NUMBER);
		});

		It("cooldown reads as expired once ActionTimer passes its expiry", [this]()
		{
			AbilityComp->SetCooldownForAbility(AbilityTag, 2.f);
			AbilityComp->ActionTimer += 3.0;             // past expiry
			AbilityComp->GenAncillaryTick(0.f, false);   // GC pass drops the stale entry
			// Note: removal itself is not observable through the public API -- Get already floors
			// a passed expiry to 0 -- so this asserts the reported value, not the map state.
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
			AbilityComp->ActionTimer += 6.0;           // advance past the 5s expiry
			AbilityComp->GenAncillaryTick(0.f, false); // GC pass drops the expired cooldown
			AbilityComp->GenPredictionTick(0.f);       // cleanup stale Ended abilities
			TestEqual("cooldown expired",
				AbilityComp->GetCooldownForAbility(AbilityTag), 0.f);
			const bool bAfterExpiry = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestTrue("activation succeeds after cooldown expires", bAfterExpiry);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
