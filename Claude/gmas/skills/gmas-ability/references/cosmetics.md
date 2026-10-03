# Cosmetics on every machine

The `UGMC_AbilitySystemComponent` FX helpers (`Public/Components/GMCAbilityComponent.h`, section *Networked FX*; bodies at the end of `Private/Components/GMCAbilityComponent.cpp`) and what to call from where. The decision table is in [SKILL.md](../SKILL.md); nothing here decides gameplay.

## Spawn API

| Function (on the ASC) | Runs where | Who sees it | Flags |
|---|---|---|---|
| `SpawnParticleSystemAtLocation(FFXSystemSpawnParameters SpawnParams, const TArray<FGMASNiagaraUserParam>& UserParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false)` → `UNiagaraComponent*` (the `UserParams` argument is 1.4+; before, `(SpawnParams, bIsClientPredicted, bDelayByGMCSmoothing)`) | the caller; on the server also `MC_SpawnParticleSystemAtLocation` (NetMulticast, Unreliable) | every machine once (receiver rule below); returns `nullptr` when the spawn was delayed or `SystemTemplate` is null (`Error` log) | `WorldContextObject` defaults to the ASC's world |
| `SpawnParticleAtPoint(UFXSystemAsset* SystemTemplate, FVector Location, FRotator Rotation, FVector Scale, UserParams, bIsClientPredicted = false, bDelayByGMCSmoothing = false)` (1.4+) | same | same | builds the params: `KeepWorldPosition`, `bAutoDestroy`, `bAutoActivate`; a zero `Scale` becomes `1` |
| `SpawnParticleSystemAttached(FFXSystemSpawnParameters SpawnParams, bool bIsClientPredicted = false, bool bDelayByGMCSmoothing = false)` → `UNiagaraComponent*` | same, through `MC_SpawnParticleSystemAttached` | same | attachment comes from the struct (`AttachToComponent`, `AttachPointName`, `LocationType`); no user params |
| `SpawnSound(USoundBase* Sound, FVector Location, float VolumeMultiplier = 1, float PitchMultiplier = 1, bool bIsClientPredicted = false)` | same, through `MC_SpawnSound`; `UGameplayStatics::PlaySoundAtLocation` | same | |
| `PlayCameraShakeAtLocation(TSubclassOf<UCameraShakeBase> ShakeClass, FVector Epicenter, float InnerRadius, float OuterRadius, float Falloff = 1, bool bOrientShakeTowardsEpicenter = false, bool bIsClientPredicted = false)` (1.4+) | same, through `MC_PlayCameraShakeAtLocation`; `UGameplayStatics::PlayWorldCameraShake` | every viewer within `OuterRadius` of the epicenter, on every machine | no smoothing delay |

`bIsClientPredicted` and `bDelayByGMCSmoothing` travel inside the multicast, so the receivers apply the caller's choice. The multicasts are unreliable: a dropped packet loses the cue on that client, which is why nothing a player decides from may live in it.

## The receiver rule

Each helper does, in order:

1. Null template or sound → `Error` log, return.
2. `HasAuthority()` → send the `MC_*` multicast with the same arguments.
3. `bDelayByGMCSmoothing && !HasAuthority() && !IsLocallyControlledPawnASC()` (a simulated proxy) → spawn after `GetTime() - GetSmoothingTime()` seconds (`UGMC_ReplicationCmp::GetTime`, `UGMC_MovementUtilityCmp::GetSmoothingTime`), return `nullptr`.
4. Otherwise spawn or play now (user params applied after the spawn).

Each `MC_*_Implementation` does: `HasAuthority()` → return (the server already played); `IsLocallyControlledPawnASC() && bIsClientPredicted` → return (the owner already played on its live move); otherwise call the helper, which on a non-authority sends no further multicast.

| Machine | `bIsClientPredicted = true` | `bIsClientPredicted = false` |
|---|---|---|
| server (dedicated or listen host; AI pawns, remote players) | plays at the call, multicasts | same |
| owning client, calling on its live move | plays at the call; ignores the multicast | plays at the call **and** again on the multicast: never call it on the owner with `false` |
| owning client, not calling (server-only cue) | plays on the multicast | plays on the multicast |
| simulated proxy | plays on the multicast, delayed when `bDelayByGMCSmoothing` | same |
| listen host's own pawn | authority: plays once, multicasts | same |

So: a cue the owner predicts is called from the shared predicted code (owner and server run it) with `true`; a cue only the server decides is called from server code with `false`; the owner never calls with `false`.

## Niagara user parameters

`FGMASNiagaraUserParam` (`Public/Utility/GMASNiagaraParams.h`, 1.4+): `Name` (the user variable's leaf name, `SizeScale`, never `User.SizeScale`), `Type` (`EGMASNiagaraUserParamType`: `Float`, `Int`, `Bool`, `Vector`, `Color`) and one value field per type (`FloatValue`, `IntValue`, `BoolValue`, `VectorValue`, `ColorValue`). Makers: `UGMASNiagaraParamLibrary::MakeNiagaraFloatParam(FName Name, float Value)` and `MakeNiagaraVectorParam(FName Name, FVector Value)`; the other types are filled by hand. The array rides in the multicast and every receiver applies it to the component it spawned (`SetVariableFloat` and friends), so remotes see the same size and colour as the caller. Values must come from bound or replicated state, never from something only the owner knows.

## The live-move guard

```cpp
// On your ability base class. Every cosmetic call from predicted code goes through it.
bool UMyAbilityBase::IsLiveMove() const
{
    const UGMC_MovementUtilityCmp* Move = GetOwnerMovementComponent();
    if (!Move || Move->IsSimulatedMove()) { return false; }            // rollback and forward-simulation executions
    return !OwnerAbilityComponent->IsReplayingForGMASLogic();          // 1.4+; Move->CL_IsReplaying() on older trees
}

void UMyAbilityBase::PlayPredictedCue(UNiagaraSystem* System, USoundBase* Sound, const FVector& At)
{
    if (!IsLiveMove()) { return; }
    TArray<FGMASNiagaraUserParam> Params;
    Params.Add(UGMASNiagaraParamLibrary::MakeNiagaraFloatParam(TEXT("SizeScale"), CueScale));   // from class data
    OwnerAbilityComponent->SpawnParticleAtPoint(System, At, FRotator::ZeroRotator, FVector::OneVector, Params,
        /*bIsClientPredicted=*/true, /*bDelayByGMCSmoothing=*/true);
    OwnerAbilityComponent->SpawnSound(Sound, At, 1.f, 1.f, /*bIsClientPredicted=*/true);
}
```

The guard is true on the owning client's first execution of a move and on the server's single execution of it, false on combined re-runs, replays and simulated executions. The helpers do not check it: a call from a replayed `BeginAbilityEvent` plays the cue again on the owner and, on the server, never (the server does not replay).

## Montages

1. Play from predicted code on both sides (`BeginAbilityEvent`): `PlayMontage_Blocking(USkeletalMeshComponent* Mesh, FGMC_MontageTracker& MontageTracker, UAnimMontage* MontageToPlay, float StartPosition = 0, float PlayRate = 1, ...)` on `UGMC_MovementUtilityCmp` (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`), with the organic movement component's `MontageTracker` (`GMCOrganicMovementComponent.h`). GMC tracks the montage as part of the move, re-checks it after a replay (`CL_CheckMontageStatusAfterReplay`) and plays it on simulated proxies itself (`PlayMontageSimulated`). Never multicast a montage.
2. Wait for its end or interruption with `UGMCAbilityTask_WaitForGMCMontageChange::WaitForGMCMontageChange(this)` (`Public/Ability/Tasks/WaitForGMCMontageChange.h`): it ticks in the prediction tick and completes with `Completed(UAnimMontage* CurrentMontage)` once `GetActiveMontage(MontageTracker)` differs from the montage running at activation; it needs a `UGMC_OrganicMovementCmp` and a running montage, otherwise it logs an `Error` and completes at once. Stop early with `StopMontage` on the same component.
3. Hit frames and notifies: bind the organic component's montage delegates (`SetMontageNotifyBeginDelegate` and the others next to it) so they run inside the move on both sides; decide outcomes on the server there.
4. State-driven animation: base the animation Blueprint on `UGMCAbilityAnimInstance` (`Public/Animation/GMCAbilityAnimInstance.h`). It finds the owner's ASC in `NativeInitializeAnimation` (retrying in `NativeBeginPlay` for Blueprint-added components; `EditorPreviewClass` for the preview), exposes `GetAbilitySystemComponent()`, and its `TagPropertyMap` (`FGMCGameplayElementTagPropertyMap`, `Public/Utility/GameplayElementMapping.h`: `PropertyMappings` of `TagsToMap` → a property) writes watched tags and attributes into anim properties from the ASC's change delegates, on the owner, the server and proxies.

## Synced events

| Call (server only) | Reaches | Use |
|---|---|---|
| `FireCustomEvent(FGameplayTag EventTag, FInstancedStruct Payload)` (1.4+) | the server and the owning client in the same logical move, through `OnCustomEvent(EventTag, Payload)`; an AI pawn on its next ancillary tick; never simulated proxies | a one-shot the owner must act on deterministically (knock-up parameters, forced reload, a cue the owner plays at the same move the server did) |
| `AddImpulse(FVector Impulse, bool bVelChange = false)` | same path | knock-backs (`gmas:gmas-rules`) |
| `SetActorLocation(FVector Location)` | same path | teleports |
| `ExecuteSyncedEvent(FGMASSyncedEventContainer)` and `OnSyncedEvent` | nobody on the 1.4 tree: the call validates its input and queues nothing | do not build on it |

A client call logs a `Warning` and returns. The payload is any `USTRUCT` in an `FInstancedStruct`; both sides read it from the delegate and integrate it themselves. Proxies see the result through bound state; a cue they must also see is one of the multicasts above, sent from the same server code.
