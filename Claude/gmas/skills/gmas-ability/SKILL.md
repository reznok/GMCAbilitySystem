---
name: gmas-ability
description: Use when creating or modifying a UGMCAbility - lifecycle hooks, cost and cooldown, activation tags, chains, tasks, input, and its visuals and sounds on every machine.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

A `UGMCAbility` (`Public/Ability/GMCAbility.h`) is a `UObject` the `UGMC_AbilitySystemComponent` ("the ASC") instantiates per activation and ticks from inside the pawn's GMC move: on the owning client (predicted, replayed) and on the server (re-executed). It owns no replicated state; what it changes lives in bound attributes and tags (`gmas:gmas-rules`), through effects (`gmas:gmas-effect`). The first ability and the movement-component wiring are in `gmas:gmas-setup`; GMC's move cycle in `gmas:gmc-prediction`; task authoring in `gmas:gmas-task`. Six worked abilities: [references/recipes.md](references/recipes.md); the cosmetics API: [references/cosmetics.md](references/cosmetics.md). GMAS paths below are relative to `Source/GMCAbilitySystem/`.

## Anatomy

Identity and cost:

| Property | Default | Meaning |
|---|---|---|
| `AbilityTag` | none | `Ability.*`; the key for cooldowns, `EndAbilitiesByTag` / `CancelAbilitiesByTag` (1.4.1+), the cancel/block containers and `OnAbilityActivated`. Not the activation tag: abilities are activated by the `Input.*` key of the ability map |
| `AbilityCost` | none | an effect class (normally `Instant`, one modifier per attribute); checked at activation (1.4.1+, below), applied by `CommitAbilityCost`. The attributes it reads and spends should be GMC-bound: the gate runs on both sides, and an unbound cost attribute logs one Warning per ability class |
| `CooldownTime` | `0` | seconds; `0` = none. Needs `AbilityTag`. Stored on the ASC as an absolute expiry in `ActionTimer` units (`ActiveCooldowns`, not bound) |
| `bApplyCooldownAtAbilityBegin` | `true` | commit the cooldown in `BeginAbility`, before your event; `false` = you call `CommitAbilityCooldown()` |
| `bAllowMultipleInstances` | `false` | `false`: a second activation while an instance is alive is refused (silently at default verbosity; `[AbilityGate]` warning once the blocker is older than 2 s) |
| `AbilityDefinition` | empty | descriptive tags matched by the `*Query` fields of other abilities and of effects (`EndAbilityOnActivationQuery`) |
| `GetAbilityCostValues()` (1.4+) | – | attribute tag → the cost's change, for UI. 1.4.1+: summed per attribute; on an instance with an owner the projected change (attribute-sourced values and `Conditions` resolved, a `Set` as target − current); on a class default object the raw `AMT_Value` amounts summed (other sources count 0). 1.4.0: only `AMT_Value` resolved and the last modifier per attribute won |

Activation gates, evaluated on the class default object before an instance exists:

| Gate | Where | Fails how |
|---|---|---|
| `bAllowMultipleInstances` | `TryActivateAbility` | returns `false`: the next candidate on the input tag is tried |
| `ActivationRequiredTags`, `ActivationBlockedTags` | `CheckActivationTags`, with `HasActiveTag` (the union of bound and client-auth tags, 1.4+) | returns `false`: next candidate |
| `ActivationQuery` | same, against the bound `ActiveTags` only | returns `false`: next candidate |
| cooldown (`IsOnCooldown()`), cost (`CanAffordAbilityCost()`, 1.4.1+), `PreExecuteCheckEvent()`, `BlockedByOtherAbility`, `IsAbilityTagBlocked` | `PreBeginAbility`, on the instance | the activation is refused: the press is consumed, no other candidate runs, the instance is `Ended` and purged next tick. A refused instance never began: no `CancelAbilityEvent`, no `OnAbilityCancelled`, no `ApplyEffectOnEnd` / `RemoveEffectOnEnd` (1.4.1+) |

Blocking and cancelling (all hierarchical on the other ability's `AbilityTag`):

| Property | Effect |
|---|---|
| `BlockedByOtherAbility` | this ability is refused while any active ability's tag matches |
| `BlockOtherAbility` | while this ability is active, candidates whose tag matches are refused (`IsAbilityTagBlocked`); `ModifyBlockOtherAbility(Add, Remove)`, `ResetBlockOtherAbility()` edit it live |
| `bBlockAllOtherAbilities` + `BlockAllAllowedTags` (1.4+) | while active, every candidate is refused unless its tag matches the allow-list; `SetBlockAllOtherAbilities(bool)` toggles it (open the gate for a recovery phase) |
| `CancelAbilitiesWithTag` | at `BeginAbility`, active abilities with matching tags are cancelled (`CancelAbilitiesByTag`, 1.4.1+: no end event, no chain window). On 1.4.0 they got `EndAbility()`, a natural end |
| `EndOtherAbilitiesQuery`, `BlockOtherAbilitiesQuery` | at `BeginAbility`, active abilities whose `AbilityDefinition` matches are flagged and cancelled on their next prediction tick (1.4.1+; `EndAbility()` on 1.4.0). The second query blocks nothing despite its name; its editor name is *End Other Abilities On Begin (Definition Query)* (1.4.1+) |

Chains and end hooks (1.4+, category *Chain*):

| Property | Effect |
|---|---|
| `ChainWindowTag`, `ChainWindowDuration` (`0`) | on a **natural** `EndAbility` (never on `CancelAbility`) the ASC applies an internal `Persistent` effect, `EffectTag` = `GrantedTags` = the window tag, `Duration` = the window, `bUniqueByEffectTag`; the next stage lists the tag in `ActivationRequiredTags` |
| `ChainConsumeWindowTags` | removed (`RemoveEffectByTagSafe`, all instances) in `BeginAbility` after every gate passed: a refused press leaves the window open |
| `ApplyEffectOnEnd`, `RemoveEffectOnEnd` | at any end of an ability that began, cancel included (1.4.1+: not for a refused activation): `ApplyAbilityEffectShort` per class, `RemoveEffectByTagSafe(Tag, -1)` per tag; `Predicted` inside a GMC tick or standalone, otherwise `PredictedQueued` |

Ticking and network:

| Property / accessor | Default | Meaning |
|---|---|---|
| `bActivateOnMovementTick` | `true` (1.4+; `false` before) | which tick *activates* the ability and dispatches its task payloads: the prediction tick (replayed) or the ancillary tick (never replayed, for what must happen once: firing a weapon). Every active ability still gets `Tick` in the prediction tick and `AncillaryTick` in the ancillary tick. The first granted candidate on an input tag decides for the batch; mixing values on one tag is an `Error` |
| `ServerConfirmTimeout` (protected, class defaults) | `2 s` (1.4+; `1 s` before) | a client instance the server has not confirmed (`RPCConfirmAbilityActivation`) within this much `ActionTimer` is cancelled (1.4.1+; ended on 1.4.0) with an `[AbilityCut]` error |
| `AbilityInputAction` | the action passed to `QueueAbility` | read by the input tasks |
| `AbilityState` (`EAbilityState`) | `PreExecution` → `Initialized` → `Ended` | `Running` and `Waiting` are never set (hidden in the editor, 1.4.1+); `IsActive()`, `AbilityEnded()` |
| `OwnerAbilityComponent`, `GetOwnerMovementComponent()` | set by `Execute` | the ASC and its `UGMC_MovementUtilityCmp`: every bound value the ability reads or writes lives on the latter |
| `GetOwnerPawn()`, `GetOwningPlayerController()` | – | the `AGMC_Pawn`; its `AGMC_PlayerController`, null for AI and on machines that do not control the pawn |
| `GetOwnerAttributeValueByTag(Tag)` | – | the attribute's `Value` (modifiers included) on the owner |
| `GetAbilityID()`, `IsServerConfirmed()` (1.4+), `GetClientStartTime()` (1.4+; `double` on 1.4.1+) | – | the activation id (operation-derived on 1.4+, identical on both sides), whether the server confirmed it, the `ActionTimer` at activation; diagnostics |

## Lifecycle

1. **Activation** (`TryActivateAbility`, ASC): instance gate, tag gates, `NewObject` from the class, `Execute(ASC, AbilityID, InputAction)` sets the owner, `AbilityInputAction`, `ClientStartTime = ActionTimer` and calls `PreBeginAbility()`. The instance is added to `ActiveAbilities` even when it already ended; the server confirms the client only when it did not.
2. **`PreBeginAbility()`**: cooldown → cost (`CanAffordAbilityCost()`, 1.4.1+) → `PreExecuteCheckEvent()` → `BlockedByOtherAbility` → `IsAbilityTagBlocked`. Each refusal logs at `Verbose` (`Ability Activation for Ability.Dash Stopped By Cooldown` / `Stopped By Cost` / ...) and refuses the activation (the press is consumed). Then (1.4.1+) `AbilityState = Initialized`, the `OnAbilityActivated` broadcast (the listener sees `IsActive()` true; a listener that calls `CancelAbility()` on it refuses the activation there: no cooldown, no `BeginAbilityEvent`, the cancel hooks fire), then `BeginAbility()`. On 1.4.0 cost was not checked and `OnAbilityActivated` fired from `BeginAbility` before `Initialized`. Keep `PreExecuteCheckEvent` cheap and pure: it runs on both sides inside the move.
3. **`BeginAbility()`**: `BlockOtherAbilitiesQuery`, chain consume, cooldown commit (`bApplyCooldownAtAbilityBegin`), `CancelConflictingAbilities()`, then **`BeginAbilityEvent()`**: your code. Override the events, not `PreBeginAbility` / `BeginAbility` / `EndAbility`, so the bookkeeping stays. An override of `BeginAbility` whose work after `Super` creates tasks, declares effects or commits a cost must stop when the ability ended inside `Super`: `Super::BeginAbility(); if (AbilityState == EAbilityState::Ended) { return; }`.
4. **`Tick(DeltaTime)`** (prediction tick, after the ASC processed the move's operation and before effects tick and attributes recalculate): confirm timeout (client), pending cancel or end, `TickTasks`, then **`TickEvent`**. **`AncillaryTick`** (ancillary tick, after cooldown expiry and task-payload dispatch): `AncillaryTickTasks`, then **`AncillaryTickEvent`**. Predicted movement goes in `TickEvent`; once-only work in `AncillaryTickEvent` or behind the live-move guard below.
5. **`EndAbility()`** is the natural end: chain window applied, `FinishEndAbility()`, **`EndAbilityEvent()`**, `OnAbilityEnded`. **`CancelAbility()`** is the abnormal end: `FinishEndAbility()`, then (1.4.1+) **`CancelAbilityEvent()`** (`BlueprintNativeEvent`) and the ASC's `OnAbilityCancelled`; no end event, no `OnAbilityEnded`, no chain window. Cleanup that must run on both paths (stop a loop, hide a cosmetic) implements both events. **First end wins** (1.4.1+): the two share one latch, so a `CancelAbility()` during a natural end, or an `EndAbility()` during a cancel, is ignored and exactly one hook set fires.
6. **`FinishEndAbility()`** (both paths, once): `[AbilityCut]` warning if tasks are unfinished, every task ended (`EndTaskGMAS`), every *declared* effect removed (by id, or by cached `EffectTag` if a replay renumbered it, `[EffectLeak]`), `ApplyEffectOnEnd` / `RemoveEffectOnEnd` (only for an ability that began, 1.4.1+), `AbilityState = Ended`. The ASC purges ended instances at the end of its next prediction tick (`CleanupStaleAbilities`; the server also sends `RPCClientEndAbility`).
7. **Ending from outside** (1.4.1+): every abnormal or external end cancels. `CancelAbilitiesByTag(Tag)` and `CancelAbilitiesByQuery(Query)` (pending cancel, next tick), `CancelAbilitiesWithTag`, `EndOtherAbilitiesQuery`, `BlockOtherAbilitiesQuery`, an effect's `CancelAbilityOnActivation` / `CancelAbilityOnEnd` and `EndAbilityOn*Query`, the client confirm timeout, the server's `RPCClientEndAbility` (`[AbilityCut]` warning) and the task heartbeat watchdog (server, 3 s without a client heartbeat) all call `CancelAbility()`. `EndAbilitiesByTag(Tag)`, `EndAbilitiesByClass(Class)` and `EndAbilitiesByQuery(Query)` (pending end) keep the natural end for callers that mean it (a death handler ending a held ability). The `By*` calls return how many live instances they reached (already-ended, unpurged ones are skipped). On 1.4.0 every one of these paths was a natural end; an ability that must not open its window on interruption detected it itself and called `CancelAbility()`.

Cost and cooldown calls:

| Call | Does | Put it |
|---|---|---|
| `CanAffordAbilityCost(DeltaTime = 1)` | per attribute, walks every modifier of the cost from the current `Value` (1.4.1+: two drains on one attribute add up, a `Set` is absolute, attribute-sourced values resolve through the owner) and answers `false` when any attribute would end below 0; `Conditions` resolved (a skipped modifier is free); unknown attributes ignored | called by the activation gate (1.4.1+) with the default `DeltaTime`, so a `Ticking` or `Periodic` cost is judged over one second at activation; call it yourself every tick for a `Ticking` cost, with the tick's delta, or at a cast's commit point. Returning it from `PreExecuteCheckEvent` is redundant on 1.4.1+ and required on 1.4.0 |
| `CommitAbilityCost()` | duplicates the cost class, applies it through the inner `ApplyAbilityEffect(Effect, Data)` (no queue type: predicted, inline), keeps `AbilityCostInstance`; a non-`Instant` cost is declared (1.4.1+), so it ends with the ability on every end path | `BeginAbilityEvent`, or at the commit point of a cast |
| `CommitAbilityCooldown()` | `SetCooldownForAbility(AbilityTag, CooldownTime)`; `0` = nothing; re-arms if already running | automatic at begin, or at the commit point with `bApplyCooldownAtAbilityBegin = false` |
| `CommitAbilityCostAndCooldown()` | both | |
| `RemoveAbilityCost()` | ends `AbilityCostInstance` at once (unqueued) and forgets its declaration (1.4.1+) | to stop a held cost before the ability ends. On 1.4.1+ a `Ticking` cost already ends with the ability; on 1.4.0 the cost instance was not declared and `CancelAbility` left it running, so apply a held cost there with `ApplyAbilityEffectSafe(..., HandlingAbility = this)` |

Effects an ability applies with `HandlingAbility = this` (or registers with `DeclareEffect(Id, QueueType)`) die with it; everything else outlives it. Read cooldowns with `GetCooldownForAbility(AbilityTag)`, `GetCooldownsForInputTag(InputTag)`, `GetMaxCooldownForAbility(Class)`.

## Activation and wiring

- **Map.** `UGMCAbilityMapData` rows (`Public/Ability/GMCAbilityMapData.h`, `FAbilityMapData`: `InputTag`, `Abilities`, `bGrantedByDefault`) listed in the ASC's `AbilityMaps` become the runtime `AbilityMap` (input tag → row) at the ASC's `BeginPlay`; default rows also add their tag to the bound `GrantedAbilityTags`. The row order is the candidate order. Runtime: `AddAbilityMapData(UGMCAbilityMapData*)` or the C++ `AddAbilityMapData(const FAbilityMapData&)`, `RemoveAbilityMapData`, `ClearAbilityMap()`.
- **Grants** are input tags: `StartingAbilities` (tags added at `BeginPlay`), `GrantAbilityByTag(Tag)` / `RemoveGrantedAbilityByTag(Tag)` / `HasGrantedAbilityTag(Tag)` and an effect's `GrantedAbilities` all take the **map key** (`Input.Dash`), whatever the parameter is called. Grant at setup identically everywhere, or on the server inside a move: the container is bound.
- **`QueueAbility(InputTag, InputAction = nullptr, bPreventConcurrentActivation = false)`** is the one entry point. It returns silently unless the caller is the autonomous proxy or the authority. On the owning client the activation operation rides the next move's bound slot and is predicted; the server re-runs it in the same move and confirms. On a server-controlled pawn it is a server operation (`gmas:gmas-rules`). `bPreventConcurrentActivation` is a local gate that saves the operation: before 1.4 it refused the queue while *any* class on the row had a live instance; since 1.4 it refuses only when *every* candidate is blocked, and a class with `bAllowMultipleInstances` never blocks.
- **`TryActivateAbilitiesByInputTag(InputTag, InputAction, bFromMovementTick, bForce, SourceOperationID)`** is what the operation runs: grant lookup (`Input tag not granted` / `Input tag not in the ability map` warnings on 1.4.1+, `Ability Tag Not Granted` / `Ability Tag Not Found ... AbilityMap` before; then `No Abilities Granted for InputTag` error), tick match on the first candidate (a mismatch keeps the payload for the ancillary tick), consumed-operation check (1.4+), then **first-passing-wins** over the candidates: `TryActivateAbility` returning `false` (instance or tag gate) tries the next; `true` ends the search even if `PreBeginAbility` then refused it (cost, cooldown, pre-check: the press is consumed). Never call it or `TryActivateAbility` on a client; both are legitimate from server-only code and from headless tests.
- **AI.** Queue from code that runs outside the pawn's move: the AI controller's `Tick` (it precedes the pawn's movement tick), a timer, a behavior node. The activation lands in the pawn's own move pipeline, not at the call site; on a networked server a pawn without an owning client gets it on its next ancillary tick (1.4+), so `BeginAbilityEvent` runs there. Never queue from inside `GenPredictionTick` or an ability tick of the same pawn.
- **`AbilityInputAction`.** Pass the `UInputAction` to `QueueAbility` (`Instance.GetSourceAction()` in the Enhanced Input handler); the input tasks (`UGMCAbilityTask_WaitForInputKeyPress::WaitForKeyPress`, `UGMCAbilityTask_WaitForInputKeyRelease::WaitForKeyRelease`) bind to it on the locally controlled pawn and complete at once without it. `UGMCAbilityTask_WaitForInputKeyPressParameterized::WaitForKeyPress` takes its own action.
- **Client-auth abilities** (`ClientAuthorizedAbilities`, 1.4+): the owning client decides, the server skips the tag gates; `bActivateOnMovementTick` must be `false`. Self-only cosmetics (an emote), nothing that touches bound state.

## Tasks

A task (`Public/Ability/Tasks/`, `UGMCAbilityTaskBase`) is how an ability waits for something or carries a value only the owner knows into the move: created with the class's static factory (`WaitDelay`, `WaitForKeyPress` / `WaitForKeyRelease` on the ability's `AbilityInputAction`, `SetTargetData<Type>`, `WaitForGameplayTagChange`, `WaitForRotateYawTowardsDirection`, `WaitForGMCMontageChange`), bound, then `ReadyForActivation()`. The ability ticks its tasks before its own event (`TickTasks` before `TickEvent`, `AncillaryTickTasks` before `AncillaryTickEvent`); a client task sends a heartbeat every second and the server cancels the ability after 3 s of silence (real time; ended on 1.4.0). Task payloads are idempotent on replay (1.4+: a finished task ignores re-delivered `Progress`); the `Completed` handlers you write are not, so guard side effects in them like any other predicted code, and create tasks only from `BeginAbilityEvent` or `Completed` handlers so both sides issue the same `TaskID`s. The full table (which tick each built-in uses, how each completes) and how to write your own: `gmas:gmas-task`.

## Replay-safe design

Replays restore bound values and re-run moves; they do **not** rewind ability instances, `ActiveCooldowns`, tasks or the operation-payload cache (`gmas:gmas-rules`). Consequences and the designs that survive them:

- **An instance that ended inside the replay window is gone.** Its `BeginAbilityEvent` effects are not reproduced; permanent writes (an `Instant` cost) are restored by the next server state. On 1.4 the re-delivered activation is skipped (`ConsumedActivationOperationIDs`); before, it was refused by the cooldown the first run armed (`IsOnCooldown()` reads the un-rewound expiry). Either way: **one short instance per press is the fragile shape.** For held or repeating actions use one long-lived instance (per life, per weapon slot) that polls a bound input flag in `TickEvent` and ends on a bound condition (death, holster), never on an unbound one.
- **Cooldowns predicted logic reads are bound floats** on the movement component (`BindSinglePrecisionFloat`, `gmas:gmc-prediction`), decremented in the prediction tick; `CooldownTime` is for abilities nothing else inspects. 1.4 stores an absolute expiry so combined moves cannot drift it, but a replay still cannot rewind it.
- **One-shot work only on the live move.** Spawns, cosmetics, RPCs, score: guard with `!OwnerAbilityComponent->IsReplayingForGMASLogic() && !GetOwnerMovementComponent()->IsSimulatedMove()` (1.4+; `GetOwnerMovementComponent()->CL_IsReplaying()` before). The server side of the same code needs only `!IsSimulatedMove()`.
- **Seal the move after a one-shot raised from bound state**: `GetOwnerMovementComponent()->CL_DoNotCombineNextMove()` (`Source/GMCCore/Public/Components/GMCReplicationComponent.h`), or the next combined re-run of the same move fires it again (`gmas:gmc-prediction`). An input bound `CombineIfUnchanged` that flips with the event achieves the same.
- **No decision from instance members.** `TickEvent` must derive what it does from bound state (attributes, tags, the movement component's bound values) and `ActionTimer`; a counter or timer kept on the ability drifts between the live run and the replay. `WaitDelay` is fine because it compares `ActionTimer`.
- **Both sides must agree on activation.** Gates on bound tags only; the same `AttributeDataAssets`, maps and grants on every machine; `bActivateOnMovementTick` identical across an input tag. A client instance the server refuses lives for `ServerConfirmTimeout` and is cancelled with `[AbilityCut]`, visibly. Put the cost on GMC-bound attributes for the same reason: the cost gate (1.4.1+) runs on both sides.

```cpp
void UMyAbility_Dash::BeginAbilityEvent_Implementation()
{
    UGMC_MovementUtilityCmp* Move = GetOwnerMovementComponent();
    const bool bLive = !OwnerAbilityComponent->IsReplayingForGMASLogic() && !Move->IsSimulatedMove();
    CommitAbilityCost();                                                   // predicted on both sides
    Move->AddImpulse(GetOwnerPawn()->GetActorForwardVector() * DashSpeed, /*bVelChange=*/true);
    if (bLive) { PlayDashCosmetics(); }                                    // once per machine
    Move->CL_DoNotCombineNextMove();
    EndAbility();
}
```

## Presentation on every machine

Every FX call made from predicted code runs up to three times on the owner (live, combined re-run, replay) and never on simulated proxies. GMAS's ASC helpers (`Public/Components/GMCAbilityComponent.h`, section *Networked FX*) solve the second half; the live-move guard solves the first. Full table and snippets: [references/cosmetics.md](references/cosmetics.md).

| Cue | Do | Who sees it when |
|---|---|---|
| **predicted cosmetic** (muzzle flash, dash trail, swing sound) | on the live move, owner and server alike: `SpawnParticleSystemAtLocation(Params, UserParams, bIsClientPredicted = true, bDelayByGMCSmoothing)` (the `UserParams` argument is 1.4+) / `SpawnSound(..., bIsClientPredicted = true)` | owner: at once. Server: multicasts `MC_*` (unreliable) then plays locally. Receivers skip when they `HasAuthority()` or when they are the locally controlled owner and `bIsClientPredicted`; everyone else plays |
| **server-decided cosmetic** (hit flash, critical sound) | server only, inside the move that decided it, `bIsClientPredicted = false` | owner and proxies when the multicast arrives, about half a round trip later |
| **server-decided gameplay cue the owner must react to at the same move** (knock-up parameters, a forced reload) | `FireCustomEvent(EventTag, Payload)` (1.4+, server); handle `OnCustomEvent` on both sides | server and owning client in the same logical move; not proxies. `ExecuteSyncedEvent` does nothing on 1.4 and is deprecated (1.4.1, removed in 1.5): do not use it |
| **state everyone displays** (aiming, stunned, charging) | a bound tag or attribute (`MatchTagToBool`, effects) | proxies through smoothing; drive visuals from `OnActiveTagsChanged` / `OnAttributeChanged` or the anim instance's `TagPropertyMap` |
| **montage** | `PlayMontage_Blocking(Mesh, MontageTracker, Montage, StartPosition, PlayRate, ...)` on the organic movement component (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`) from predicted code on both sides; wait with `WaitForGMCMontageChange` | GMC predicts it, replays it and reproduces it on simulated proxies; never multicast a montage yourself |

Rules:

- **FX never inside a replayed or simulated move.** Guard every spawn with the live-move condition above; the ASC helpers do not check it for you.
- **Simulated proxies display the past.** A multicast arrives at server time while the proxy shows a state a smoothing delay old; `bDelayByGMCSmoothing = true` holds the cue on proxies for `GetTime() - GetSmoothingTime()` so it lands where the pawn is shown.
- **Niagara user parameters** travel with the multicast: build `FGMASNiagaraUserParam` entries (`Public/Utility/GMASNiagaraParams.h`, `UGMASNiagaraParamLibrary::MakeNiagaraFloatParam(Name, Value)` / `MakeNiagaraVectorParam`; leaf names, without `User.`) so every receiver sets the same size or colour. `SpawnParticleAtPoint(System, Location, Rotation, Scale, UserParams, ...)` (1.4+) builds the spawn params for you; `SpawnParticleSystemAttached` has no user params.
- **Multicasts are unreliable and carry no state.** Anything a player *decides* from (a reload finished, a charge ready) is bound state; the multicast is only its sparkle.
- **Animation.** Base the animation Blueprint on `UGMCAbilityAnimInstance` (`Public/Animation/GMCAbilityAnimInstance.h`): it finds the owner's ASC (`GetAbilitySystemComponent()`) and its `TagPropertyMap` (`FGMCGameplayElementTagPropertyMap`, `Public/Utility/GameplayElementMapping.h`) copies watched tags and attributes into anim properties from the change delegates, on every machine including proxies.

## Checklist

- Gates: the cost is affordable at activation (checked for you on 1.4.1+; `PreExecuteCheckEvent` returns `CanAffordAbilityCost()` on 1.4.0) and its attributes are GMC-bound; `ActivationRequiredTags` / `ActivationBlockedTags` hold bound tags only; `bAllowMultipleInstances` chosen; `bActivateOnMovementTick` identical for every class on the input tag.
- Cost effect is `Instant` with one modifier per attribute and committed once (`CommitAbilityCost` / `CommitAbilityCostAndCooldown`); a `Ticking` cost ends with the ability (declared, 1.4.1+; on 1.4.0 applied with `HandlingAbility = this`).
- Cooldown: `CooldownTime` only when nothing in the prediction tick reads it; otherwise a bound float on the movement component.
- Lifetime: no instance state the replay cannot rebuild; held and repeating actions use one long-lived instance polling a bound flag; the ability ends on bound conditions.
- Side effects only on live moves (`!IsReplayingForGMASLogic() && !IsSimulatedMove()`), followed by `CL_DoNotCombineNextMove()`.
- Ends: `EndAbility` for natural ends, `CancelAbility` for interruptions; cleanup that must run on both paths lives in `EndAbilityEvent` and `CancelAbilityEvent` (1.4.1+); every effect the ability must take with it is declared; external ends are cancels on 1.4.1+ (`CancelAbilitiesWithTag`, effects' `CancelAbilityOn*`, timeouts) and natural ends on 1.4.0; a `BeginAbility` override returns when `AbilityState == Ended` after `Super`.
- Chains (1.4+): stage N opens `ChainWindowTag` for `ChainWindowDuration`; stage N+1 requires and consumes it; stage N blocks on it while open; stages listed later-first in the map row.
- Presentation: predicted cosmetics with `bIsClientPredicted = true` on the live move; server-only cues with `false`; `bDelayByGMCSmoothing` for proxies; owner-only gameplay cues through `FireCustomEvent`; montages through GMC.
- Tags registered: `Ability.*` for `AbilityTag`, `Input.*` for the map row and `QueueAbility`, `State.*` for gates and windows.
- Verified under networked PIE with a client (`gmas:gmas-testing`): no `[AbilityCut]`, no `Stopped By` lines at `Verbose` for a legitimate press, no corrections on activation or end.

## Which test to write

A spec in the style of `Source/GMCAbilitySystemTests/Private/GMAS_AbilitySpec.cpp` (`GMAS.Unit.Ability`), `GMAS_ActivationSpec.cpp`, `GMAS_ChainSpec.cpp` and `GMAS_AbilityEndSpec.cpp` (`GMAS.Unit.AbilityEnd`, 1.4.1+: cancel versus end, the dead-born rule, first end wins; `Private/Tests/` in the runtime module before 1.4.1), on the stub harness `gmas:gmas-testing` describes (ownerless stub, CDO configured in setup and restored in teardown), with your real `UGMCAttributesData` rows when the ability has a cost. Register the row with the C++ `AddAbilityMapData(FAbilityMapData)`, activate with `TryActivateAbility(UMyAbility_Dash::StaticClass())` (no client exists here) and assert the gate results: `GetActiveAbilityCount`, the instance's `AbilityState`, `GetCooldownForAbility`, `GetAttributeValueByTag` after the cost, `HasActiveTag` for a chain window. On 1.4.1+ drive time with the seams: `SetActionTimerForTest`, `TickActiveAbilitiesForTest(Dt)` and `CleanupStaleAbilitiesForTest()` (before: write `ActionTimer`, `GenPredictionTick(0.f)` to purge, the instance's own `Tick(Dt)`); `GenAncillaryTick(Dt, false)` expires cooldowns. Cover one case per gate (an unaffordable cost included) and one per end path (`EndAbilitiesByTag` vs `CancelAbilitiesByTag`: window opened or not, which event fired, declared effect gone). Anything the ability spawns, hits or multicasts, and every prediction claim (confirm, replay, `[AbilityCut]`), needs a networked PIE test with a client (`gmas:gmas-testing`).
