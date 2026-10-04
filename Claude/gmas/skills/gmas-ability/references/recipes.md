# Ability recipes

Six `UGMCAbility` subclasses as short sketches: the class defaults to set (only what leaves the defaults), the map row, and the code that matters. Lifecycle, gates and the cosmetics rules are in [SKILL.md](../SKILL.md); the effects named here follow `gmas:gmas-effect`; the bound values on `UMyMovementCmp` are bound as `gmas:gmc-prediction` describes (inputs `ClientAuth_Input` + `CombineIfUnchanged`, state `ServerAuth_Output_ClientValidated`). `IsLiveMove()` is the guard from [cosmetics.md](cosmetics.md); `HasBoundActiveTag` (1.4+) reads the bound container alone, which is what `HasActiveTag` did on older trees. Every class keeps `bActivateOnMovementTick = true` (set it explicitly on trees older than 1.4) unless stated.

## 1. Instant impulse: `UMyAbility_Dash`

| Field | Value |
|---|---|
| `AbilityTag` | `Ability.Dash` |
| `AbilityCost` | `UMyEffect_DashCost` (`Instant`, `Attribute.Stamina` `-20`) |
| `CooldownTime` | `1.5` (nothing else reads it; a HUD-visible cooldown would be a bound float instead) |
| `ActivationBlockedTags` | `State.Stunned` |
| map row | `Input.Dash` → `UMyAbility_Dash` |

```cpp
// The activation refuses an unaffordable AbilityCost (1.4.1+); on 1.4.0 also override
// PreExecuteCheckEvent_Implementation() to return CanAffordAbilityCost().
void UMyAbility_Dash::BeginAbilityEvent_Implementation()
{
    UGMC_MovementUtilityCmp* Move = GetOwnerMovementComponent();
    CommitAbilityCost();                                                        // the cooldown was committed by BeginAbility
    Move->AddImpulse(GetOwnerPawn()->GetActorForwardVector() * DashSpeed, /*bVelChange=*/true);
    if (IsLiveMove()) { PlayPredictedCue(DashFx, DashSound, GetOwnerPawn()->GetActorLocation()); }
    Move->CL_DoNotCombineNextMove();                                            // the impulse must not be re-run by a combined move
    EndAbility();
}
```

The whole ability runs inside one prediction tick on the owner and the server: the cost and the impulse are predicted, the replay re-runs them from the restored state. The instance ends in the same call, so it is the short-lived shape the replay cannot rebuild (`SKILL.md`, replay-safe design); that is acceptable here because everything it changed is bound (`RawValue` of stamina, velocity) and a lost cosmetic on a replayed press is invisible.

## 2. Held: `UMyAbility_Sprint`

| Field | Value |
|---|---|
| `AbilityTag` | `Ability.Sprint` |
| `ActivationBlockedTags` | `State.Stunned` |
| `bAllowMultipleInstances` | `false` (default) |
| map row | `Input.Sprint` → `UMyAbility_Sprint`; `UMyMovementCmp` binds `bInput_Sprint` and sets it from the held input |

```cpp
void UMyAbility_Sprint::BeginAbilityEvent_Implementation()
{
    // UMyEffect_Sprint: Persistent, bNegateEffectAtEnd, GrantedTags State.Sprinting, +MaxSpeed.
    // UMyEffect_SprintDrain: Ticking, Attribute.Stamina -15 per second. Both declared to this ability
    // (HandlingAbility), so EndAbility and CancelAbility alike remove them.
    bool bOk = false; int Handle = -1, Id = -1; UGMCAbilityEffect* Effect = nullptr;
    OwnerAbilityComponent->ApplyAbilityEffectSafe(UMyEffect_Sprint::StaticClass(), FGMCAbilityEffectData(),
        EGMCAbilityEffectQueueType::Predicted, bOk, Handle, Id, Effect, this);
    OwnerAbilityComponent->ApplyAbilityEffectSafe(UMyEffect_SprintDrain::StaticClass(), FGMCAbilityEffectData(),
        EGMCAbilityEffectQueueType::Predicted, bOk, Handle, Id, Effect, this);
}

void UMyAbility_Sprint::TickEvent_Implementation(float DeltaTime)
{
    const UMyMovementCmp* Move = Cast<UMyMovementCmp>(GetOwnerMovementComponent());
    const bool bHeld = Move->bInput_Sprint && Move->Velocity.SizeSquared() > 1.f;        // bound input, bound velocity
    if (!bHeld || GetOwnerAttributeValueByTag(StaminaTag) <= 0.f) { EndAbility(); }       // bound conditions only
}
```

Activated once on the press (`QueueAbility(Input.Sprint)` from the *Started* event), it lives while the bound flag is held and ends on bound conditions, so a replay that re-runs its ticks reaches the same decision at the same move. Do not use `WaitForInputKeyRelease` to end it: that task needs an Enhanced Input action and the key's live state, neither of which a replay has. The alternative cost shape (`AbilityCost` = the ticking drain, `CommitAbilityCost()` at begin, `CanAffordAbilityCost(DeltaTime)` per tick) works too: the activation gate judges the drain over one second, and on 1.4.1+ the committed cost is declared, so every end path removes it. On 1.4.0 it was not declared: call `RemoveAbilityCost()` from both `EndAbilityEvent` and an override of `CancelAbility`.

## 3. Charge or cast with cost at commit: `UMyAbility_Overcharge`

| Field | Value |
|---|---|
| `AbilityTag` | `Ability.Overcharge` |
| `AbilityCost` | `UMyEffect_OverchargeCost` (`Instant`, `Attribute.Stamina` `-30`) |
| `CooldownTime`, `bApplyCooldownAtAbilityBegin` | `8`, `false` (committed with the cost, not at begin) |
| `ActivationBlockedTags` | `State.Stunned` |
| map row | `Input.Overcharge` → `UMyAbility_Overcharge` |

```cpp
void UMyAbility_Overcharge::BeginAbilityEvent_Implementation()   // activation already refused an unaffordable cost (1.4.1+)
{
    UGMCAbilityTask_WaitDelay* Cast = UGMCAbilityTask_WaitDelay::WaitDelay(this, CastTime);    // ActionTimer-based
    Cast->Completed.AddDynamic(this, &UMyAbility_Overcharge::OnCastFinished);
    Cast->ReadyForActivation();
}

void UMyAbility_Overcharge::TickEvent_Implementation(float DeltaTime)
{
    if (OwnerAbilityComponent->HasBoundActiveTag(StunnedTag)) { CancelAbility(); }           // interrupted: nothing committed
}

void UMyAbility_Overcharge::OnCastFinished()                                                 // UFUNCTION()
{
    if (!CanAffordAbilityCost()) { CancelAbility(); return; }                                  // re-check at the commit point
    CommitAbilityCostAndCooldown();
    OwnerAbilityComponent->ApplyAbilityEffectShort(UMyEffect_Haste::StaticClass(), EGMCAbilityEffectQueueType::Predicted);
    EndAbility();
}
```

`WaitDelay` compares `ActionTimer`, so the cast finishes in the same logical move on both sides and a replay recounts it. The cost and the cooldown are committed only when the cast completes: an interrupted cast costs nothing. A stun that arrives as an effect with `CancelAbilityOnActivation = Ability.Overcharge` cancels the ability (1.4.1+: `CancelAbilityEvent` runs, no end event, no chain window; on 1.4.0 it was a natural `EndAbility`, and the explicit `CancelAbility` in `TickEvent` was the only way to keep the end event and the window from running). Hide the cast's cosmetics in both `EndAbilityEvent` and `CancelAbilityEvent`.

## 4. Per-life persistent ability polling a bound flag: `UMyAbility_Gun`

| Field | Value |
|---|---|
| `AbilityTag` | `Ability.Gun` |
| `CooldownTime`, `bApplyCooldownAtAbilityBegin` | `0`, `false` (the fire interval is bound) |
| `bAllowMultipleInstances` | `false` |
| map row | `Input.Gun` → `UMyAbility_Gun`; `UMyMovementCmp` binds `bInput_Fire`, `FireCooldown` (float) and `ShotCounter` (int) |

```cpp
// Activated once per life by whoever controls the pawn: the owning client queues Input.Gun when its input is set
// up, an AI controller when it possesses. The death code ends it inside the move: EndAbilitiesByTag(Ability.Gun).
void UMyAbility_Gun::TickEvent_Implementation(float DeltaTime)
{
    UMyMovementCmp* Move = Cast<UMyMovementCmp>(GetOwnerMovementComponent());
    Move->FireCooldown = FMath::Max(0.f, Move->FireCooldown - DeltaTime);
    if (!Move->bInput_Fire || Move->FireCooldown > 0.f || OwnerAbilityComponent->HasBoundActiveTag(StunnedTag)) { return; }

    Move->FireCooldown = FireInterval;                 // bound: the replay restores it and re-arms it at the same move
    Move->ShotCounter += 1;                            // bound: the shot's identity, identical on every machine
    if (IsLiveMove()) { SpawnShot(Move->ShotCounter); }   // projectile, muzzle flash, sound: once per machine
    Move->CL_DoNotCombineNextMove();                   // the next frame starts a fresh move after the shot
}
```

One instance per life is the activation; every press is bound input the tick reads. Nothing about the weapon lives on the instance: the interval, the counter and the trigger are bound, so the replay reproduces the shots' timing exactly and the HUD reads `FireCooldown` without a second source. The server confirmed the instance at activation, so `ServerConfirmTimeout` never applies; a mid-life replay cannot lose the instance because it never ends inside a replay window. If the gun must also be a cooldown-gated "ability" in the UI, derive the UI from the bound float, not from `GetCooldownForAbility`.

## 5. Chained combo: `UMyAbility_Slash` → `UMyAbility_Slash2` (1.4+)

| Field | Stage 1 `UMyAbility_Slash` | Stage 2 `UMyAbility_Slash2` |
|---|---|---|
| `AbilityTag` | `Ability.Slash` | `Ability.Slash2` |
| `ChainWindowTag`, `ChainWindowDuration` | `State.Chain.Slash2`, `0.6` | – |
| `ActivationRequiredTags` | – | `State.Chain.Slash2` |
| `ChainConsumeWindowTags` | – | `State.Chain.Slash2` |
| `ActivationBlockedTags` | `State.Chain.Slash2` (no restart while the window is open), `State.Stunned` | `State.Stunned` |
| `CooldownTime` | `0` | `1.0` (the combo's recovery) |
| map row | `Input.Attack` → `[UMyAbility_Slash2, UMyAbility_Slash]`: stage 2 first, so it wins while the window is open and falls through to stage 1 when its required tag is missing | |

```cpp
void UMyAbility_Slash::BeginAbilityEvent_Implementation()
{
    UGMCAbilityTask_WaitDelay* Swing = UGMCAbilityTask_WaitDelay::WaitDelay(this, SwingTime);
    Swing->Completed.AddDynamic(this, &UMyAbility_Slash::OnSwingFinished);
    Swing->ReadyForActivation();
    if (IsLiveMove()) { PlayPredictedCue(SwingFx, SwingSound, GetOwnerPawn()->GetActorLocation()); }
}

void UMyAbility_Slash::TickEvent_Implementation(float DeltaTime)
{
    if (OwnerAbilityComponent->HasBoundActiveTag(StunnedTag)) { CancelAbility(); }    // no window for an interrupted swing
}

void UMyAbility_Slash::OnSwingFinished() { EndAbility(); }                          // natural end: opens State.Chain.Slash2
```

The hit itself is decided on the server in the swing's move (a trace at the hit frame, damage through a `ServerInstantAttribute` effect on the victim, `gmas:gmas-effect`); the client predicts only the swing and its cue. A press during the window activates stage 2, which removes the window effect in `BeginAbility`; a press refused by any gate leaves it open; after `0.6 s` the window's own `Duration` ends it and the next press is stage 1 again. Both stages share `bActivateOnMovementTick`. The window is an ordinary effect with `bUniqueByEffectTag`: on the 1.4 tree a second grant while it is open is rejected rather than refreshed (`gmas:gmas-effect`), which the stage-1 block above makes unreachable.

## 6. AI-queued ability: `AMyBotController`

| Field | Value |
|---|---|
| controller | `AGMC_AIController` subclass; the pawn, its map and its abilities are the player's classes |
| activation | `QueueAbility` from the controller's `Tick`, which runs before the pawn's movement tick |

```cpp
void AMyBotController::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);
    AMyPawn* Bot = Cast<AMyPawn>(GetPawn());
    if (!Bot || !HasAuthority()) { return; }
    if (WantsToDash(Bot))                                         // decided from the server's view of the world
    {
        // Outside the pawn's move: the operation reaches its own move pipeline (next ancillary tick on a networked
        // server, 1.4+; the next move in standalone), never the call site. The gates run there, as for a player.
        Bot->AbilitySystemComponent->QueueAbility(TAG_Input_Dash, nullptr, /*bPreventConcurrentActivation=*/true);
    }
}
```

Never queue from inside the pawn's own `GenPredictionTick`, an ability tick or an effect hook of the same pawn: those run inside the move the operation would have to land in. The ability runs on the server only, so `GetOwningPlayerController()` is null, there is no `AbilityInputAction` and the input tasks complete at once: give AI abilities bound flags to poll (the controller writes the pawn's input, as a player's input component would) instead of input tasks. Cosmetics reach every client through the `MC_*` multicasts the server sends from the live move; `bIsClientPredicted` is irrelevant without an owner. `bPreventConcurrentActivation = true` saves an operation while the previous instance is still alive.
