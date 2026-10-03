# Networked PIE test skeleton

A latent-command harness for automation tests that need a dedicated server and clients in one editor process: a session request with explicit settings (`MyPIE`), a phased command (`FMyNetTest`) that waits until every player has a pawn, runs steps, ends the session and waits for it to close, and one example test with a confirm window. Copy it into an editor-capable module (`UncookedOnly`, or `Editor`; the module links `UnrealEd` for editor targets plus `GMCAbilitySystem` and `GMCCore`), rename, and replace the three project hooks: the map, `AMyPawn`, and `AMyPawn::GetAbilitySystem()` (your accessor to the pawn's `UGMC_AbilitySystemComponent`). The reasoning behind each piece is in the skill (`SKILL.md`, *Networked PIE tests*); the comments here say why a line is the way it is. Engine signatures: `Editor/UnrealEd/Classes/Settings/LevelEditorPlaySettings.h` and `LevelEditorPlayNetworkEmulationSettings.h` in the same folder, `Editor/UnrealEd/Public/PlayInEditorDataTypes.h`, `Editor/UnrealEd/Classes/Editor/EditorEngine.h`, `Runtime/Core/Public/Misc/AutomationTest.h`, all relative to `Engine/Source/`.

Every file below is wrapped in `#if WITH_AUTOMATION_TESTS && WITH_EDITOR` (after `#include "Misc/AutomationTest.h"`): the harness needs `GEditor`, and nothing in it belongs in a cooked game.

## MyPIE: the session

```cpp
// MyPIE.h
#pragma once
#include "CoreMinimal.h"

/** One session request. Every field is set explicitly: the harness never inherits the user's last Play settings. */
struct FMyPIEOptions
{
    int32 NumClients  = 1;    // 0 = standalone (one local player, no server); 1..N = a dedicated server plus N clients
    int32 RttMs       = 0;    // emulated round trip per client (ms), split between its outgoing and incoming packets
    int32 LossPercent = 0;    // emulated packet loss per client, each direction
    FString Map = TEXT("/Game/Maps/L_NetTest");     // the test's map, never whatever is open in the editor
};

namespace MyPIE
{
    bool AnySession();                                            // a PIE world exists: running, or still closing
    bool Start(const FMyPIEOptions& Options, FString& OutError);  // requests the session; it starts over the next frames
    void End();                                                   // requests the end; poll AnySession() until it is false
}
```

```cpp
// MyPIE.cpp
#include "MyPIE.h"
#include "Editor.h"                               // GEditor
#include "Editor/EditorEngine.h"                  // RequestPlaySession, RequestEndPlayMap, IsPlaySession*
#include "Engine/Engine.h"                        // GEngine->GetWorldContexts()
#include "Settings/LevelEditorPlaySettings.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
    // The queued request keeps a plain pointer to the settings object and the session starts a few frames
    // later; nothing else references the object, so hold it here until the next request replaces it.
    TStrongObjectPtr<ULevelEditorPlaySettings> GPlaySettings;
}

bool MyPIE::AnySession()
{
    if (!GEngine) { return false; }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        if (Context.WorldType == EWorldType::PIE && Context.World()) { return true; }
    }
    return false;
}

bool MyPIE::Start(const FMyPIEOptions& Options, FString& OutError)
{
    if (!GEditor) { OutError = TEXT("no editor"); return false; }
    // Every in-process session binds the same server port, so there is room for exactly one: refuse a second
    // while one runs, is queued (IsPlaySessionInProgress covers both), or is still closing. The test's Start
    // phase waits for that to clear.
    if (AnySession() || GEditor->IsPlaySessionInProgress())
    {
        OutError = TEXT("a PIE session is already running or queued");
        return false;
    }
    const bool bNetworked = Options.NumClients > 0;
    // NewObject copies the config-loaded defaults, i.e. the user's last Play menu choices: override everything
    // the test depends on, so a run means the same thing on every machine.
    ULevelEditorPlaySettings* Settings = NewObject<ULevelEditorPlaySettings>();
    Settings->SetPlayNetMode(bNetworked ? EPlayNetMode::PIE_Client : EPlayNetMode::PIE_Standalone); // PIE_Client = windowless dedicated server + clients
    Settings->SetPlayNumberOfClients(FMath::Max(1, Options.NumClients));
    Settings->SetRunUnderOneProcess(true);                 // all worlds in this process: the test reads server and clients alike
    Settings->bLaunchSeparateServer = false;
    Settings->NetworkEmulationSettings = FLevelEditorPlayNetworkEmulationSettings();   // drop the user's emulation profile
    Settings->NetworkEmulationSettings.CurrentProfile = TEXT("Custom");                // "Custom" uses the packet values below as set
    if (bNetworked && (Options.RttMs > 0 || Options.LossPercent > 0))
    {
        FLevelEditorPlayNetworkEmulationSettings& Emu = Settings->NetworkEmulationSettings;
        Emu.bIsNetworkEmulationEnabled = true;
        Emu.EmulationTarget = NetworkEmulationTarget::Client;   // clients only: the server stays clean, each client sees the whole round trip
        Emu.OutPackets.MinLatency = Emu.OutPackets.MaxLatency = Options.RttMs / 2;               // constant latency: runs stay comparable
        Emu.InPackets.MinLatency  = Emu.InPackets.MaxLatency  = Options.RttMs - Options.RttMs / 2;
        Emu.OutPackets.PacketLossPercentage = Options.LossPercent;
        Emu.InPackets.PacketLossPercentage  = Options.LossPercent;
    }
    GPlaySettings.Reset(Settings);

    FRequestPlaySessionParams Params;
    Params.EditorPlaySettings = Settings;
    Params.GlobalMapOverride  = Options.Map;
    GEditor->RequestPlaySession(Params);
    return true;
}

void MyPIE::End()
{
    if (GEditor) { GEditor->RequestEndPlayMap(); }
}
```

## FMyNetTest: the phased command

```cpp
// MyNetTest.h
#pragma once
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"
#include "MyPIE.h"

class AMyPawn;
class UWorld;                                              // neither CoreMinimal.h nor AutomationTest.h declares it

/**
 * A PIE test as a latent command: Start -> WaitReady -> Run -> End -> WaitClosed. Derive, implement RunStep(),
 * enqueue with ADD_LATENT_AUTOMATION_COMMAND. Worlds are looked up again every frame: a cached UWorld* dangles
 * the moment the session ends, and the world list changes while clients connect.
 */
class FMyNetTest : public IAutomationLatentCommand
{
public:
    FMyNetTest(FAutomationTestBase* InTest, const FMyPIEOptions& InOptions) : Test(InTest), Options(InOptions) {}
    virtual bool Update() override final;                 // once per editor frame; true when the command is finished

protected:
    virtual bool RunStep() = 0;                            // one frame of the test once ready; return true when done
    virtual bool IsReady();                                // polled every frame; must hold for SettleSeconds before Run
    virtual void Cleanup() {}                              // after the session closed: restore anything the test changed
    bool Fail(const FString& Message);                     // records the error and ends the session; returns true: `return Fail(...)`
    void NextStep() { ++Step; StepStart = Now; }
    double StepSeconds() const { return Now - StepStart; } // the clock of every confirm window
    UWorld* Server() const { return ServerWorld; }         // the dedicated server's world (the one world in a standalone session)
    UWorld* Client(int32 Index) const { return ClientWorlds.IsValidIndex(Index) ? ClientWorlds[Index] : nullptr; } // client 1 = 0

    // Time budgets in seconds. Plain members, not constants: a derived test widens one in its constructor
    // (a long respawn, a slow first map load) without touching the harness.
    double ReadyTimeout   = 120.0;                         // the first session compiles shaders and loads the map
    double RunTimeout     = 180.0;
    double CloseTimeout   = 30.0;
    double SettleSeconds  = 2.0;                           // after everyone has a pawn: spawn-smoothing pause, first server states, clock sync
    double ConfirmSeconds = 6.0;                           // a round trip, the 1 s server-operation grace, smoothing, margin

    FAutomationTestBase* Test = nullptr;
    FMyPIEOptions Options;
    int32 Step = 0;

private:
    enum class EPhase : uint8 { Start, WaitReady, Run, End, WaitClosed, Done };
    void ResolveWorlds();

    EPhase Phase = EPhase::Start;
    double Now = 0.0, PhaseStart = 0.0, StepStart = 0.0, ReadySince = -1.0;
    UWorld* ServerWorld = nullptr;                         // valid only during the frame that resolved it
    TArray<UWorld*> ClientWorlds;
};

namespace MyNet
{
    AMyPawn* LocalPawn(UWorld* World);                                   // the local player's pawn in World, or null
    AMyPawn* ServerTwin(UWorld* ServerWorld, const AMyPawn* ClientPawn); // the server's copy of a client's pawn, or null
}
```

```cpp
// MyNetTest.cpp
#include "MyNetTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "MyPawn.h"

AMyPawn* MyNet::LocalPawn(UWorld* World)
{
    if (!World) { return nullptr; }
    for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
    {
        if (APlayerController* PC = It->Get(); PC && PC->IsLocalController()) { return Cast<AMyPawn>(PC->GetPawn()); }
    }
    return nullptr;
}

AMyPawn* MyNet::ServerTwin(UWorld* ServerWorld, const AMyPawn* ClientPawn)
{
    // Pair by a replicated identity: actor pointers never cross worlds, and pawns are replaced on respawn.
    const APlayerState* PS = ClientPawn ? ClientPawn->GetPlayerState() : nullptr;
    if (!ServerWorld || !PS) { return nullptr; }
    for (FConstPlayerControllerIterator It = ServerWorld->GetPlayerControllerIterator(); It; ++It)
    {
        const APlayerController* PC = It->Get();
        if (PC && PC->PlayerState && PC->PlayerState->GetPlayerId() == PS->GetPlayerId()) { return Cast<AMyPawn>(PC->GetPawn()); }
    }
    return nullptr;
}

void FMyNetTest::ResolveWorlds()
{
    ServerWorld = nullptr;
    ClientWorlds.Reset();
    if (!GEngine) { return; }
    for (const FWorldContext& Context : GEngine->GetWorldContexts())
    {
        UWorld* World = Context.World();
        if (Context.WorldType != EWorldType::PIE || !World) { continue; }
        switch (World->GetNetMode())
        {
        case NM_DedicatedServer: case NM_Standalone: ServerWorld = World; break;
        case NM_Client: ClientWorlds.Add(World); break;    // context order is connection order: client 1, 2, ...
        default: break;
        }
    }
}

bool FMyNetTest::IsReady()
{
    if (!ServerWorld || ClientWorlds.Num() < Options.NumClients) { return false; }
    for (int32 i = 0; i < FMath::Max(1, Options.NumClients); ++i)
    {
        const AMyPawn* Pawn = MyNet::LocalPawn(Options.NumClients > 0 ? Client(i) : ServerWorld);
        if (!Pawn || !Pawn->GetAbilitySystem()) { return false; }
        if (Options.NumClients > 0 && !MyNet::ServerTwin(ServerWorld, Pawn)) { return false; }   // possession has replicated both ways
    }
    return true;
}

bool FMyNetTest::Fail(const FString& Message)
{
    Test->AddError(Message);
    if (Phase == EPhase::WaitReady || Phase == EPhase::Run) { Phase = EPhase::End; }   // close the session: the next test needs the port
    return true;
}

bool FMyNetTest::Update()
{
    Now = FPlatformTime::Seconds();
    if (Phase == EPhase::WaitReady || Phase == EPhase::Run) { ResolveWorlds(); }      // every frame, never cached across frames
    switch (Phase)
    {
    case EPhase::Start:
    {
        if (PhaseStart == 0.0) { PhaseStart = Now; }
        if (MyPIE::AnySession())                                                        // the previous test's session is still closing
        {
            if (Now - PhaseStart > CloseTimeout) { Test->AddError(TEXT("a previous PIE session never closed")); Phase = EPhase::Done; return true; }
            return false;
        }
        FString Error;
        if (!MyPIE::Start(Options, Error)) { Test->AddError(TEXT("PIE session refused: ") + Error); Phase = EPhase::Done; return true; }
        PhaseStart = Now;
        Phase = EPhase::WaitReady;
        return false;
    }
    case EPhase::WaitReady:
    {
        const bool bReady = IsReady();
        if (Phase != EPhase::WaitReady) { return false; }                               // a derived IsReady() called Fail()
        if (bReady)
        {
            if (ReadySince < 0.0) { ReadySince = Now; }
            if (Now - ReadySince >= SettleSeconds) { Phase = EPhase::Run; PhaseStart = StepStart = Now; }
        }
        else { ReadySince = -1.0; }                                                      // readiness must hold continuously: a respawn restarts the settle
        if (Phase == EPhase::WaitReady && Now - PhaseStart > ReadyTimeout) { Fail(TEXT("players not ready within the timeout")); }
        return false;
    }
    case EPhase::Run:
        if (RunStep()) { Phase = EPhase::End; }
        else if (Phase == EPhase::Run && Now - PhaseStart > RunTimeout) { Fail(FString::Printf(TEXT("steps did not finish (step %d)"), Step)); }
        return false;
    case EPhase::End:
        MyPIE::End();
        PhaseStart = Now;
        Phase = EPhase::WaitClosed;
        return false;
    case EPhase::WaitClosed:
    {
        const bool bClosed = !MyPIE::AnySession();
        if (!bClosed && Now - PhaseStart <= CloseTimeout) { return false; }
        if (!bClosed) { Test->AddError(TEXT("PIE session did not close within the timeout")); }
        ServerWorld = nullptr;                                                           // from here on every world pointer is garbage
        ClientWorlds.Reset();
        Cleanup();
        Phase = EPhase::Done;
        return true;
    }
    default:
        return true;
    }
}
```

## Example: a predicted dash agrees with the server under lag

```cpp
// MyDashNetTest.cpp
#include "Misc/AutomationTest.h"
#if WITH_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/GMCAbilityComponent.h"
#include "MyNetTest.h"
#include "MyPawn.h"

/** Client 1 dashes under 75 ms / 1 % loss: the owner predicts the stamina spend, the server spends the same in the same move, both end the ability. */
class FMyDashSpendsStaminaCommand : public FMyNetTest
{
public:
    explicit FMyDashSpendsStaminaCommand(FAutomationTestBase* InTest) : FMyNetTest(InTest, FMyPIEOptions{ 1, 75, 1 }) {}

protected:
    virtual bool RunStep() override
    {
        // Re-find both pawns every step: a death or travel inside the window replaces them.
        AMyPawn* Mine = MyNet::LocalPawn(Client(0));
        AMyPawn* Twin = Mine ? MyNet::ServerTwin(Server(), Mine) : nullptr;
        if (!Mine || !Twin) { return Fail(TEXT("lost client 1's pawn or its server twin")); }
        UGMC_AbilitySystemComponent* ClientASC = Mine->GetAbilitySystem();
        UGMC_AbilitySystemComponent* ServerASC = Twin->GetAbilitySystem();
        switch (Step)
        {
        case 0:
            StaminaBefore = ServerASC->GetAttributeValueByTag(StaminaTag);
            ClientASC->QueueAbility(InputDashTag);        // the owner's input: rides its next move; the server re-runs it one round trip later
            NextStep();
            return false;
        case 1:
        {
            // Confirm window: poll until both sides show the spend and the end, or the window closes; assert exactly once either way.
            const float Expected = StaminaBefore - DashCost;
            const bool bClient = FMath::IsNearlyEqual(ClientASC->GetAttributeValueByTag(StaminaTag), Expected, 0.01f);
            const bool bServer = FMath::IsNearlyEqual(ServerASC->GetAttributeValueByTag(StaminaTag), Expected, 0.01f);
            const bool bEnded  = ClientASC->GetActiveAbilityCountByTag(AbilityDashTag) == 0 && ServerASC->GetActiveAbilityCountByTag(AbilityDashTag) == 0;
            if ((bClient && bServer && bEnded) || StepSeconds() > ConfirmSeconds)
            {
                Test->TestTrue(TEXT("client 1 predicted the stamina spend"), bClient);
                Test->TestTrue(TEXT("the server spent the same stamina"), bServer);
                Test->TestTrue(TEXT("the dash ended on both sides"), bEnded);
                return true;
            }
            return false;
        }
        default:
            return true;
        }
    }

private:
    static constexpr float DashCost = 25.f;               // the cost effect's modifier; keep it next to the asset it mirrors
    const FGameplayTag InputDashTag   = FGameplayTag::RequestGameplayTag(TEXT("Input.Dash"));
    const FGameplayTag AbilityDashTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Dash"));
    const FGameplayTag StaminaTag     = FGameplayTag::RequestGameplayTag(TEXT("Attribute.Stamina"));
    float StaminaBefore = 0.f;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMyDashSpendsStaminaTest, "MyGame.PIE.Networked.DashSpendsStamina",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FMyDashSpendsStaminaTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FMyDashSpendsStaminaCommand(this));   // the test "passes" here; the command carries the asserts
    return true;
}
#endif
```

Run it alone or with its siblings with the same headless command as the specs, narrowed to the prefix: `-ExecCmds="Automation RunTests MyGame.PIE.Networked;Quit" -unattended -nullrhi -log` (the PIE worlds tick without rendering), or from the Session Frontend. A negative claim ("no second projectile", "no replay burst") uses the same window shape but waits the whole `ConfirmSeconds` before asserting.
