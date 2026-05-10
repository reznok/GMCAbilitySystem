// Layer 2 tests: client-authoritative ability activation and effect application —
// whitelist gating, EffectID range allocation, server-side anti-cheat checks, and
// the BlockedByOtherAbility coherence rule that survives the client-auth bypass.
//
// Activation flow under client-auth (per spec design plan_gmas_client_auth.md §5):
//   QueueAbility(Tag)
//     → IsClientAuthorizedAbility(Class)?           → yes : client-auth path
//        → CheckActivationTagsForClientAuth(CDO)    → BlockedByOther + Cooldown only
//        → TryActivateAbility(..., bSkipActivationTagsCheck=true)
//        → BoundQueueV2.QueueClientOperation(ClientAuthAbilityActivationOp)
//     → no                                         → standard flow

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestAbilityB.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASClientAuthSpec,
    "GMAS.Unit.ClientAuth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

    UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
    UGMC_AbilitySystemComponent* AbilityComp = nullptr;

    FGameplayTag AbilityTagA;
    FGameplayTag AbilityTagB;

    void SetupHarness();
    void TeardownHarness();

END_DEFINE_SPEC(FGMASClientAuthSpec)

void FGMASClientAuthSpec::SetupHarness()
{
    static FNativeGameplayTag SAbilityTagA(
        TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
        TEXT("GMAS.Test.ClientAuth.Wave"), TEXT("Wave emote tag for client-auth tests"),
        ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
    static FNativeGameplayTag SAbilityTagB(
        TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
        TEXT("GMAS.Test.ClientAuth.Salute"), TEXT("Salute emote tag for client-auth tests"),
        ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
    AbilityTagA = SAbilityTagA.GetTag();
    AbilityTagB = SAbilityTagB.GetTag();

    MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
    MoveCmp->AddToRoot();

    AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
    AbilityComp->AddToRoot();
    AbilityComp->GMCMovementComponent = MoveCmp;
    AbilityComp->BindReplicationData();
    // Positive ActionTimer required so subsequent tasks can allocate EffectIDs via
    // ActionTimer*100 (GetNextAvailableEffectID returns -1 if ActionTimer == 0).
    // Diverges intentionally from the -1.0 sentinel used in GMAS_ActivationSpec.
    AbilityComp->ActionTimer = 1.0;

    GetMutableDefault<UGMAS_TestAbility>()->AbilityTag = AbilityTagA;
    GetMutableDefault<UGMAS_TestAbility>()->CooldownTime = 0.f;
    GetMutableDefault<UGMAS_TestAbility>()->bAllowMultipleInstances = false;
    // Client-auth abilities MUST NOT run on the movement tick (replay incoherence otherwise).
    // Forced false here so all tests in this spec match the production contract.
    GetMutableDefault<UGMAS_TestAbility>()->bActivateOnMovementTick = false;

    GetMutableDefault<UGMAS_TestAbilityB>()->AbilityTag = AbilityTagB;
    GetMutableDefault<UGMAS_TestAbilityB>()->CooldownTime = 0.f;
    GetMutableDefault<UGMAS_TestAbilityB>()->bAllowMultipleInstances = false;
    GetMutableDefault<UGMAS_TestAbilityB>()->bActivateOnMovementTick = false;
}

void FGMASClientAuthSpec::TeardownHarness()
{
    auto ResetCDO = [](UGMCAbility* CDO)
    {
        CDO->AbilityTag                = FGameplayTag();
        CDO->ActivationRequiredTags    = FGameplayTagContainer();
        CDO->ActivationBlockedTags     = FGameplayTagContainer();
        CDO->BlockOtherAbility         = FGameplayTagContainer();
        CDO->BlockedByOtherAbility     = FGameplayTagContainer();
        CDO->CancelAbilitiesWithTag    = FGameplayTagContainer();
        CDO->CooldownTime              = 0.f;
        CDO->bAllowMultipleInstances   = false;
        CDO->bActivateOnMovementTick   = true;  // SetupHarness forces false for client-auth tests; restore the engine default
    };
    ResetCDO(GetMutableDefault<UGMAS_TestAbility>());
    ResetCDO(GetMutableDefault<UGMAS_TestAbilityB>());

    if (AbilityComp)
    {
        AbilityComp->bForceAuthorityForTest = false;
    }

    if (AbilityComp) { AbilityComp->RemoveFromRoot(); AbilityComp = nullptr; }
    if (MoveCmp)     { MoveCmp->RemoveFromRoot();     MoveCmp = nullptr; }
}

void FGMASClientAuthSpec::Define()
{
    // ── Client-Authoritative activation ──────────────────────────────────────────
    Describe("Client-Authoritative GMAS", [this]()
    {
        BeforeEach([this]() { SetupHarness(); });
        AfterEach([this]() { TeardownHarness(); });

        It("returns false from IsClientAuthorizedAbility when the class is not in the whitelist", [this]()
        {
            TestFalse(TEXT("Empty whitelist must return false"),
                AbilityComp->IsClientAuthorizedAbility(UGMAS_TestAbility::StaticClass()));
        });

        It("returns true from IsClientAuthorizedAbility when the class is in the whitelist", [this]()
        {
            AbilityComp->ClientAuthorizedAbilities.Add(UGMAS_TestAbility::StaticClass());
            TestTrue(TEXT("Whitelisted ability must return true"),
                AbilityComp->IsClientAuthorizedAbility(UGMAS_TestAbility::StaticClass()));
        });

        It("returns false from IsClientAuthorizedEffect when the class is not in the whitelist", [this]()
        {
            TestFalse(TEXT("Empty effect whitelist must return false"),
                AbilityComp->IsClientAuthorizedEffect(UGMCAbilityEffect::StaticClass()));
        });

        It("returns true from IsClientAuthorizedEffect when the class is in the whitelist", [this]()
        {
            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());
            TestTrue(TEXT("Whitelisted effect must return true"),
                AbilityComp->IsClientAuthorizedEffect(UGMCAbilityEffect::StaticClass()));
        });

        It("allocates a client-auth EffectID in the reserved high range", [this]()
        {
            AbilityComp->ActionTimer = 1.0;
            const int ID = AbilityComp->GetNextAvailableClientAuthEffectID();
            TestTrue(TEXT("Client-auth ID must be >= ClientAuthEffectIDOffset"),
                ID >= UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
        });

        It("returns -1 when ActionTimer is zero (cannot allocate)", [this]()
        {
            AbilityComp->ActionTimer = 0.0;
            AddExpectedError(TEXT("ActionTimer is 0"), EAutomationExpectedErrorFlags::Contains, 1);
            const int ID = AbilityComp->GetNextAvailableClientAuthEffectID();
            TestEqual(TEXT("ActionTimer=0 returns sentinel -1"), ID, -1);
        });

        It("standard EffectIDs remain in the low range", [this]()
        {
            AbilityComp->ActionTimer = 1.0;
            const int ID = AbilityComp->GetNextAvailableEffectID();
            TestTrue(TEXT("Standard ID must be < ClientAuthEffectIDOffset"),
                ID < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
        });

        It("skips ActivationRequiredTags check when bSkipActivationTagsCheck=true", [this]()
        {
            static FNativeGameplayTag SRequired(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.RequiresLife"), TEXT("Test required tag"),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            GetMutableDefault<UGMAS_TestAbility>()->ActivationRequiredTags.AddTag(SRequired.GetTag());

            // Standard call: should fail because RequiredTag not present.
            const bool bStandard = AbilityComp->TryActivateAbility(
                UGMAS_TestAbility::StaticClass(), nullptr, FGameplayTag::EmptyTag, /*bSkipActivationTagsCheck=*/false);
            TestFalse(TEXT("Standard call must fail with missing required tag"), bStandard);

            // Skip call: should succeed.
            const bool bSkip = AbilityComp->TryActivateAbility(
                UGMAS_TestAbility::StaticClass(), nullptr, FGameplayTag::EmptyTag, /*bSkipActivationTagsCheck=*/true);
            TestTrue(TEXT("Skip call must succeed despite missing required tag"), bSkip);
        });

        // Tests added here in subsequent tasks.

        It("rejects ClientAuth effect apply when not whitelisted", [this]()
        {
            AddExpectedError(TEXT("ClientAuth apply rejected"), EAutomationExpectedErrorFlags::Contains, 1);

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), {},
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestFalse(TEXT("Non-whitelisted ClientAuth apply must fail"), bSuccess);
        });

        It("applies ClientAuth effect with high-range EffectID when whitelisted", [this]()
        {
            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), {},
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestTrue(TEXT("Whitelisted ClientAuth apply must succeed"), bSuccess);
            TestTrue(TEXT("EffectID must be in client-auth range"),
                Id >= UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
        });

        It("CheckActivationTagsForClientAuth ignores ActivationRequiredTags", [this]()
        {
            static FNativeGameplayTag SRequired(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.RequireBypass"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
            CDO->ActivationRequiredTags.AddTag(SRequired.GetTag());
            TestTrue(TEXT("Should pass even though required tag is missing"),
                AbilityComp->CheckActivationTagsForClientAuthForTest(CDO));
        });

        It("CheckActivationTagsForClientAuth honors BlockedByOtherAbility", [this]()
        {
            static FNativeGameplayTag SBlocker(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.Blocker"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
            CDO->BlockedByOtherAbility.AddTag(SBlocker.GetTag());

            // Activate the blocker so the tag is in the active blocking set.
            UGMAS_TestAbilityB* CDOB = GetMutableDefault<UGMAS_TestAbilityB>();
            CDOB->BlockOtherAbility.AddTag(SBlocker.GetTag());
            const bool bBlockerActivated = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass(), nullptr,
                FGameplayTag::EmptyTag, /*bSkipActivationTagsCheck=*/true);
            TestTrue(TEXT("Blocker ability must activate for this test to be meaningful"), bBlockerActivated);

            TestFalse(TEXT("Should fail when blocked by an active ability"),
                AbilityComp->CheckActivationTagsForClientAuthForTest(CDO));
        });

        It("rejects ClientAuth remove with standard-range ID (anti-cheat)", [this]()
        {
            AddExpectedError(TEXT("not in client-auth range"), EAutomationExpectedErrorFlags::Contains, 1);

            const bool bOk = AbilityComp->RemoveEffectByIdSafe({100}, EGMCAbilityEffectQueueType::ClientAuth);
            TestFalse(TEXT("Standard-range ID must be rejected by ClientAuth remove"), bOk);
        });

        // QueueAbility's role gate (ROLE_AutonomousProxy / ROLE_Authority) prevents direct
        // invocation in headless tests where the component has no owner. The full routing
        // from QueueAbility -> client-auth path is validated in the PIE smoke test (Task 13).
        // Here we validate the helper that QueueAbility delegates to once routing succeeds.
        It("TryActivateClientAuthAbility activates a whitelisted ability via the reduced gate set", [this]()
        {
            UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
            CDO->bActivateOnMovementTick = false;
            AbilityComp->ClientAuthorizedAbilities.Add(UGMAS_TestAbility::StaticClass());

            AbilityComp->AddAbilityMapData({AbilityTagA, {UGMAS_TestAbility::StaticClass()}});
            AbilityComp->GrantAbilityByTag(AbilityTagA);

            // Force authority so the helper takes the HasAuthority() early-return branch
            // (no client->server BoundQueueV2 payload, but the local ability still activates).
            AbilityComp->bForceAuthorityForTest = true;

            const bool bActivated = AbilityComp->TryActivateClientAuthAbilityForTest(
                UGMAS_TestAbility::StaticClass(), AbilityTagA, nullptr);
            TestTrue(TEXT("Helper must report success for a whitelisted ability"), bActivated);
            TestEqual(TEXT("Whitelisted ability must be active after the helper call"),
                AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
        });

        It("server rejects ClientAuth ability operation when class not whitelisted", [this]()
        {
            AddExpectedError(TEXT("not whitelisted"), EAutomationExpectedErrorFlags::Contains, 1);

            AbilityComp->bForceAuthorityForTest = true;
            AbilityComp->SeedBoundQueueOperationDataForTest(-1);

            FGMASBoundQueueV2ClientAuthAbilityActivationOperation Op;
            Op.OperationID = -1;
            Op.AbilityClass = UGMAS_TestAbility::StaticClass();  // NOT in whitelist
            Op.InputTag = AbilityTagA;
            Op.InputAction = nullptr;

            FInstancedStruct InstancedOp = FInstancedStruct::Make(Op);
            AbilityComp->ServerProcessOperationForTest(InstancedOp, false);

            TestEqual(TEXT("Spoofed activation must not produce an active ability"),
                AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
        });

        It("server activates whitelisted client-auth ability without RPCConfirm", [this]()
        {
            UGMAS_TestAbility* CDO = GetMutableDefault<UGMAS_TestAbility>();
            CDO->bActivateOnMovementTick = false;
            AbilityComp->ClientAuthorizedAbilities.Add(UGMAS_TestAbility::StaticClass());
            AbilityComp->AddAbilityMapData({AbilityTagA, {UGMAS_TestAbility::StaticClass()}});
            AbilityComp->GrantAbilityByTag(AbilityTagA);

            AbilityComp->bForceAuthorityForTest = true;
            AbilityComp->SeedBoundQueueOperationDataForTest(-1);

            FGMASBoundQueueV2ClientAuthAbilityActivationOperation Op;
            Op.OperationID = -1;
            Op.AbilityClass = UGMAS_TestAbility::StaticClass();
            Op.InputTag = AbilityTagA;
            Op.InputAction = nullptr;

            FInstancedStruct InstancedOp = FInstancedStruct::Make(Op);
            AbilityComp->ServerProcessOperationForTest(InstancedOp, false);

            TestEqual(TEXT("Whitelisted activation must produce an active ability"),
                AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
        });

        It("server rejects ClientAuth effect with non-client-auth ID range (anti-cheat)", [this]()
        {
            AddExpectedError(TEXT("non-client-auth ID range"), EAutomationExpectedErrorFlags::Contains, 1);

            // Whitelist the effect class so we reach the ID-range check (not the whitelist rejection).
            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            // Build the spoofed operation: EffectID is below ClientAuthEffectIDOffset.
            FGMASBoundQueueV2ClientAuthEffectOperation Op;
            Op.EffectClass = UGMCAbilityEffect::StaticClass();
            Op.EffectID    = 50;   // < ClientAuthEffectIDOffset → must be rejected
            Op.EffectData  = {};
            // Client ops use negative OperationIDs; any non-zero value satisfies the guard.
            Op.OperationID = -1;

            FInstancedStruct InstancedOp = FInstancedStruct::Make(Op);

            // Headless harness: orphan component has no world/owner so HasAuthority()==false
            // and BoundQueueV2.OperationData is uninitialised. Both seams are needed to reach
            // the dispatch branch under test.
            AbilityComp->bForceAuthorityForTest = true;
            AbilityComp->SeedBoundQueueOperationDataForTest(-1);

            AbilityComp->ServerProcessOperationForTest(InstancedOp, false);

            TestFalse(TEXT("Spoofed ID effect must NOT be in ActiveEffects"),
                AbilityComp->GetActiveEffects().Contains(50));

            AbilityComp->bForceAuthorityForTest = false;
        });

        // ── Tag routing: GrantedTags from ClientAuth effects must NOT pollute the bound state ──

        It("routes ClientAuth effect GrantedTags to ClientAuthActiveTags, not ActiveTags", [this]()
        {
            static FNativeGameplayTag SFooTag(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.GrantedFoo"), TEXT("Granted tag for routing tests"),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            const FGameplayTag FooTag = SFooTag.GetTag();

            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            FGMCAbilityEffectData Data;
            Data.EffectTag = FooTag;
            Data.GrantedTags.AddTag(FooTag);
            Data.EffectType = EGMASEffectType::Persistent;

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), Data,
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestTrue(TEXT("Apply must succeed"), bSuccess);

            TestFalse(TEXT("Bound container must NOT have the tag (no GMC divergence)"),
                AbilityComp->HasBoundActiveTag(FooTag));
            TestTrue(TEXT("Union query (HasActiveTag) sees the tag"),
                AbilityComp->HasActiveTag(FooTag));
            TestTrue(TEXT("ClientAuth getter exposes the tag"),
                AbilityComp->GetClientAuthActiveTags().HasTag(FooTag));
        });

        // ── Symmetric remove ───────────────────────────────────────────────────────

        It("removes the tag from ClientAuthActiveTags when the effect ends", [this]()
        {
            static FNativeGameplayTag SBarTag(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.GrantedBar"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            const FGameplayTag BarTag = SBarTag.GetTag();

            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            FGMCAbilityEffectData Data;
            Data.EffectTag = BarTag;
            Data.GrantedTags.AddTag(BarTag);
            Data.EffectType = EGMASEffectType::Persistent;

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), Data,
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestTrue(TEXT("Pre-condition: apply succeeded"), bSuccess);
            TestTrue(TEXT("Pre-condition: tag is present"), AbilityComp->HasActiveTag(BarTag));

            AbilityComp->RemoveActiveAbilityEffect(Effect);

            TestFalse(TEXT("Tag fully cleaned from union after remove"),
                AbilityComp->HasActiveTag(BarTag));
            TestFalse(TEXT("ClientAuth container no longer carries the tag"),
                AbilityComp->GetClientAuthActiveTags().HasTag(BarTag));
        });

        // ── No GMC bound state mutation ────────────────────────────────────────────

        It("apply via ClientAuth does not mutate the bound ActiveTags snapshot", [this]()
        {
            static FNativeGameplayTag SBazTag(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.GrantedBaz"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            const FGameplayTag BazTag = SBazTag.GetTag();

            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            const FGameplayTagContainer SnapshotBefore = AbilityComp->GetBoundActiveTags();

            FGMCAbilityEffectData Data;
            Data.EffectTag = BazTag;
            Data.GrantedTags.AddTag(BazTag);
            Data.EffectType = EGMASEffectType::Persistent;

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), Data,
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestTrue(TEXT("Pre-condition: apply succeeded"), bSuccess);

            const FGameplayTagContainer SnapshotAfter = AbilityComp->GetBoundActiveTags();
            TestTrue(TEXT("Bound ActiveTags container is byte-for-byte identical before/after apply"),
                SnapshotBefore == SnapshotAfter);
        });

        // ── Change delegates can observe client-auth tag transitions ───────────────
        // Note: OnActiveTagsChanged uses DECLARE_DYNAMIC_MULTICAST_DELEGATE (UObject-only
        // binding); lambdas cannot bind directly in headless tests. Instead we validate
        // the necessary condition for the delegate to fire: GetActiveTags() (the union
        // queried by CheckActiveTagsChanged at runtime) must reflect the new tag. If this
        // holds, the delegate path naturally observes the transition during the next tick.

        It("GetActiveTags() reflects the client-auth tag so CheckActiveTagsChanged sees it", [this]()
        {
            static FNativeGameplayTag SQuxTag(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.GrantedQux"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            const FGameplayTag QuxTag = SQuxTag.GetTag();

            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            FGMCAbilityEffectData Data;
            Data.EffectTag = QuxTag;
            Data.GrantedTags.AddTag(QuxTag);
            Data.EffectType = EGMASEffectType::Persistent;

            bool bSuccess; int Handle, Id; UGMCAbilityEffect* Effect;
            AbilityComp->ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), Data,
                EGMCAbilityEffectQueueType::ClientAuth, bSuccess, Handle, Id, Effect, nullptr);
            TestTrue(TEXT("Pre-condition: apply succeeded"), bSuccess);

            TestTrue(TEXT("GetActiveTags() (union) includes the client-auth tag"),
                AbilityComp->GetActiveTags().HasTag(QuxTag));
            TestFalse(TEXT("GetBoundActiveTags() does NOT include the client-auth tag"),
                AbilityComp->GetBoundActiveTags().HasTag(QuxTag));
        });

        // ── Server-side mirror: dispatcher routes the tag identically ──────────────

        It("server-side ClientAuthEffectOperation dispatch routes GrantedTags to ClientAuthActiveTags", [this]()
        {
            static FNativeGameplayTag SServerTag(
                TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
                TEXT("GMAS.Test.ClientAuth.ServerGranted"), TEXT(""),
                ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
            const FGameplayTag ServerTag = SServerTag.GetTag();

            AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());

            // Build the operation as if it arrived from the client.
            FGMASBoundQueueV2ClientAuthEffectOperation Op;
            Op.EffectClass = UGMCAbilityEffect::StaticClass();
            Op.EffectID    = UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset + 200;
            Op.EffectData.EffectTag = ServerTag;
            Op.EffectData.GrantedTags.AddTag(ServerTag);
            Op.EffectData.EffectType = EGMASEffectType::Persistent;
            Op.OperationID = -1;

            FInstancedStruct InstancedOp = FInstancedStruct::Make(Op);

            AbilityComp->bForceAuthorityForTest = true;
            AbilityComp->SeedBoundQueueOperationDataForTest(-1);

            AbilityComp->ServerProcessOperationForTest(InstancedOp, false);

            TestFalse(TEXT("Server bound container must remain untouched"),
                AbilityComp->HasBoundActiveTag(ServerTag));
            TestTrue(TEXT("Server client-auth container received the tag via dispatcher"),
                AbilityComp->GetClientAuthActiveTags().HasTag(ServerTag));

            AbilityComp->bForceAuthorityForTest = false;
        });
    });
}

#endif // WITH_AUTOMATION_WORKER
