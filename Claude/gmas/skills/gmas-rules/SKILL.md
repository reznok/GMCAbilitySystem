---
name: gmas-rules
description: Use when changing gameplay that involves a GMC_AbilitySystemComponent (GMAS) - deciding where state lives (bound, replicated, derived), attribute binding, ability lifetime under replay, effect queue types, server operations, impulses and tags.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

GMAS (GMC Ability System) is an ability system built on GMC's predicted move cycle instead of on RPCs. `UGMC_AbilitySystemComponent` (`Source/GMCAbilitySystem/Public/Components/GMCAbilityComponent.h`, "the ASC") binds its attributes, tags and operation queue into the pawn's move, so abilities and effects are predicted, validated and replayed by the same machinery as movement. Read `gmas:gmc-prediction` first if moves, replays and bound values are new to you; this skill holds the rules every other `gmas:*` skill links to. GMAS paths below are relative to `Source/GMCAbilitySystem/`.

## How GMAS rides GMC

The ASC is not a GMC component and does nothing on its own: the pawn's movement component (`UGMC_MovementUtilityCmp` or a subclass) forwards five of its hooks to it.

| GMC hook on the movement component | ASC call | What the ASC does there |
|---|---|---|
| `BindReplicationData_Implementation()` (`Source/GMCCore/Public/Components/GMCReplicationComponent.h`) | `BindReplicationData()` | resolves a null `GMCMovementComponent` from the owner and refuses with an Error when there is none (1.4.1+), instantiates attributes from `AttributeDataAssets`, binds every bound attribute's `Value` and `RawValue`, the granted-ability and active tag containers, the task-data slot, the operation-queue slot and the active-effect id list (1.4+) |
| `PreLocalMoveExecution_Implementation(const FGMC_Move&)` (same header) | `PreLocalMoveExecution()` | moves the next queued task payload and the next queued operation into their bound slots so they travel with this move |
| `GenPredictionTick_Implementation(float DeltaTime)` (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`) | `GenPredictionTick(DeltaTime)` | sets `ActionTimer` from the move timestamp, processes the operation in the bound slot (activations, effects, impulses, events), ticks abilities and effects, recalculates bound attributes; replayed |
| `GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove)` (same header) | `GenAncillaryTick(DeltaTime, bCombinedClientMove)` | the non-replayed half: activations and task payloads of abilities with `bActivateOnMovementTick = false`, every active ability's `AncillaryTick`, cooldown expiry, change delegates (`OnAttributeChanged`, `OnActiveTagsChanged`), the server-operation grace countdown, and on the server `StartingEffects` once the pawn has a controller (1.4.1+; polled from the prediction tick on 1.4.0) |
| `GenSimulationTick_Implementation(float DeltaTime)` (same header) | `GenSimulationTick(DeltaTime)` | change delegates on simulated proxies, teleport smoothing |

```cpp
void UMyMovementCmp::BindReplicationData_Implementation()
{
    Super::BindReplicationData_Implementation();
    BindSinglePrecisionFloat(DashCooldown, /* modes */);      // the game's own values first
    AbilitySystem->BindReplicationData();                     // GMAS binds LAST
}

void UMyMovementCmp::GenPredictionTick_Implementation(float DeltaTime)
{
    Super::GenPredictionTick_Implementation(DeltaTime);
    AbilitySystem->MatchTagToBool(StunnedTag, StunTimer > 0.f); // bound tag follows bound state
    AbilitySystem->GenPredictionTick(DeltaTime);               // then abilities, effects, attributes
}

void UMyMovementCmp::GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove)
{
    Super::GenAncillaryTick_Implementation(DeltaTime, bLocalMove, bCombinedClientMove);
    AbilitySystem->GenAncillaryTick(DeltaTime, bCombinedClientMove);
}
```

`GenSimulationTick` and `PreLocalMoveExecution` forward the same way. The ASC binds **last** by convention: it reads the movement component and binds a tag-sorted, variable-length block (one pair of floats per bound attribute, plus containers) after the game's fixed ones. The order of the ASC call relative to the game's own logic inside each hook is a design choice; keep it identical on every machine.

## Where does this state live?

Decide this before writing a `UPROPERTY`. "Predicted logic" is anything that runs inside `GenPredictionTick` on either side: movement, ability ticks, effect ticks, activation checks.

| The value is… | Keep it as | How |
|---|---|---|
| read by predicted logic in any move (stamina a dash spends, the cooldown the tick checks, a stun that stops movement, a speed cap) | **GMC-bound** | an attribute with `bGMCBound = true`; a state tag kept with `MatchTagToBool`; or a value the movement component binds itself (`gmas:gmc-prediction`) |
| decided by the server and only displayed elsewhere (health under server-side damage, score, kills, respawn timer) | **replicated** | an unbound attribute (`bGMCBound = false`, sent through `UnBoundAttributes`), or `UPROPERTY(Replicated)` on the pawn or player state; changed on the server only, through `ServerAuth` / `ServerInstantAttribute` effects or plain server code |
| derivable on every machine from identity and configuration (max speed from a loadout, weapon parameters from an archetype) | **derived** | computed from the replicated identity plus data assets on each machine; nothing is sent. `GetExternalModifierValue` (1.4+) is the hook for feeding such values into modifiers |
| a one-shot the server imposes on a pawn (knock-back, forced effect, teleport) | **server operation** | through the target's ASC (`AddImpulse`, a `ServerAuth` effect, `FireCustomEvent`); it lands inside the target's move |
| purely visual (muzzle flash, trail, hit flash, HUD cache) | **local** | spawned on live moves, in `GenSimulationTick`, from change delegates, or through the `MC_*` cosmetic helpers |

Two rules follow:

- **Never `DOREPLIFETIME` (or make an unbound attribute of) something predicted logic reads.** A replicated property arrives between moves, is never restored by a replay, and gives client and server two different values for the same move: every read is a correction waiting to happen.
- **Never bind something only the HUD or a cosmetic reads.** A bound value costs bandwidth in every move and state, splits combined moves whenever it changes (`CombineIfUnchanged`), and corrects the client every time the two sides disagree about a number nobody acts on. Replicate it, or derive it from bound values on the display side.

Worked example, `UMyAbility_Dash` on `AMyPawn`:

| State | Home | Because |
|---|---|---|
| `Attribute.Stamina` the dash spends, clamped by `Attribute.MaxStamina` | bound attributes | the prediction tick checks and spends it |
| the dash cooldown the tick compares against | bound float on `UMyMovementCmp` | predicted logic reads it; GMAS cooldowns are not rewound |
| `State.Stunned` blocking the dash | bound tag via `MatchTagToBool` | an activation gate evaluated in the move |
| dash distance and cost | derived from the loadout data asset | identical on every machine, never changes mid-life |
| dashes performed, shown on the scoreboard | replicated on the player state | the server counts, nobody predicts it |
| dash trail and sound | local, on the live move + cosmetic multicast | nobody decides anything from it |

## Attributes

Attributes are declared in `UGMCAttributesData` assets (`Public/Attributes/GMCAttributesData.h`, `FAttributeData`) listed in the ASC's `AttributeDataAssets`; the runtime struct is `FAttribute` (`Public/Attributes/GMCAttributes.h`).

| `FAttributeData` field | Default | Rule |
|---|---|---|
| `AttributeTag` | – | `Attribute.*` |
| `bGMCBound` | `true` | `true` only for attributes predicted logic reads. A bound attribute binds `Value` and `RawValue` as `ServerAuth_Output_ClientValidated`, `Periodic_Output`: both sides compute them in the move, a mismatch corrects the client, simulated proxies receive them through smoothing. An unbound attribute replicates through `UnBoundAttributes` (`OnRep_UnBoundAttributes`) and cannot take part in prediction |
| `DefaultValue`, `bStartFull` (1.4+) | `0`, `false` | `bStartFull` starts the attribute at its resolved upper clamp (needs `Clamp.Max` or `MaxAttributeTag`) |
| `Clamp` (`FAttributeClamp`, `Public/Attributes/GMCAttributeClamp.h`) | `bClampMin` / `bClampMax` `true` (1.4+), `Min` / `Max` `0`, tags empty | **The default clamp holds the attribute at 0.** Set `Max` or `MaxAttributeTag` (`Attribute.MaxStamina`), or untick `bClampMax`; same for the floor. On trees older than 1.4 a clamp whose bounds are all zero is ignored instead |
| `ValueCombineMode` (1.4+) | `CombineIfUnchanged` | leave it. `AlwaysCombineOverwrite` only for an attribute that changes every tick (continuous regen) and does not feed the movement integration |

- **Data before binding.** `BindReplicationData()` reads `AttributeDataAssets` and binds the attributes sorted by tag. Fill `AttributeDataAssets` (and `StartingEffects`, `StartingAbilities`, `StartingTags`) in the constructor, in class defaults, or in `PostInitializeComponents` before the call that runs the GMC bind, identically on every machine: a different attribute list on client and server corrupts every state they exchange. Initial values can be overridden per attribute in `SetAttributeInitialValue` / `OnInitializeAttributeInitialValue`.
- **Change bound attributes inside the move, on both sides.** Through effects (`gmas:gmas-effect`) or `ApplyAbilityAttributeModifier(const FGMCAttributeModifier&)` (`Op` `Add`, `Set`, `SetReplace` (1.4+), …) called from predicted logic on the owning client and the server alike; the result is validated like any bound value. Unbound attributes change on the server only.
- **`SetAttributeValueByTag(Tag, NewValue, bResetModifiers)` is deprecated and inert** (removed in 1.5). It changes nothing; 1.4.0 returned `true`, 1.4.1+ returns `false` and logs once. Do not use it; initial values come from the data asset or `SetAttributeInitialValue`, changes from a `Set` modifier.
- Read with `GetAttributeValueByTag` (with temporal modifiers) or `GetAttributeRawValue` (permanent part only). `OnAttributeChanged(Tag, Old, New)` fires from the ancillary and simulation ticks once per real change, on every machine: the hook for HUD and cosmetics, never for gameplay decisions.

## Abilities under replay

- **Activation is by input tag.** `QueueAbility(InputTag, InputAction = nullptr, bPreventConcurrentActivation = false)` from input or AI code. On the owning client the activation rides the next move's bound slot and is predicted; the server re-runs it inside the same move. On a server-controlled pawn (AI, the listen-server host) the same call becomes a server operation (below). Never call `TryActivateAbilitiesByInputTag` / `TryActivateAbility` directly on a client. Granted abilities come from `AbilityMaps` plus `StartingAbilities` / `GrantAbilityByTag` (a bound container: grant at setup, or on the server inside a move).
- **`bActivateOnMovementTick`** (`Public/Ability/GMCAbility.h`, default `true` on 1.4+, `false` before): which tick *activates* the ability and dispatches its task payloads: the prediction tick (replayed), or with `false` the ancillary tick (never replayed: for what must happen exactly once, firing a weapon). The flag does not move the ability's own ticks: every active ability gets `Tick` / `TickEvent` in the prediction tick and `AncillaryTick` in the ancillary tick (`TickActiveAbilities`, `TickAncillaryActiveAbilities`, `Private/Components/GMCAbilityComponent.cpp`). Every ability sharing an input tag must use the same value; the first granted candidate decides which tick the batch runs on.
- **Replays do not rewind ability instances or cooldowns.** `ActiveAbilities` and `ActiveCooldowns` are plain members, not bound. A replay re-executes the moves with their recorded operations, so an instance that ended inside the replay window is gone: nothing re-creates it and its effects are not reproduced. GMAS 1.4 refuses to re-activate an operation it already consumed (`ConsumedActivationOperationIDs`) rather than build a phantom twin; on older trees the re-delivered activation is refused by the cooldown the first run set (`UGMCAbility::PreBeginAbility` sees `IsOnCooldown()` and calls `CancelAbility()`, `Private/Ability/GMCAbility.cpp`). A client ability the server never confirms is cut after `ServerConfirmTimeout` (2 s on 1.4+, 1 s before) with an `[AbilityCut]` error: cancelled on 1.4.1+ (no end event, no chain window), ended on 1.4.0. Activation gates (tags, cooldown, and on 1.4.1+ the cost) run on both sides, so everything they read is bound.
- **Patterns that survive this:**
  - long-lived instances (one per life, per weapon slot) that poll a bound input flag every tick, instead of one short instance per press;
  - cooldowns the predicted logic reads kept as bound floats on the movement component; `CooldownTime` / `ActiveCooldowns` only for abilities nothing else inspects (1.4 stores an absolute expiry, so combined moves cannot drift it);
  - one-shot work (spawns, RPCs, score) only on live moves: `!IsReplayingForGMASLogic() && !GMCMovementComponent->IsSimulatedMove()` (1.4+; `CL_IsReplaying()` on the movement component before), then `CL_DoNotCombineNextMove()` (`gmas:gmc-prediction`);
  - `UGMCAbilityTask_WaitForInputKeyRelease` (`Public/Ability/Tasks/WaitForInputKeyRelease.h`) listens to the ability's Enhanced Input action and completes at once without one: an ability driven by a bound held flag polls the flag instead.

Recipes: `gmas:gmas-ability`.

## Effects

`EGMCAbilityEffectQueueType` (`Public/Components/GMCAbilityComponent.h`) decides who applies an effect and where. Apply with `ApplyAbilityEffect(Class, Data, QueueType, OutHandle, OutId, OutEffect)` or `ApplyAbilityEffectShort(Class, QueueType, HandlingAbility)`; removal mirrors it (`RemoveEffectByTagSafe`, `RemoveEffectByIdSafe`, with a queue type).

| Queue type | Who calls | What happens |
|---|---|---|
| `Predicted` | owner and server, inside a move | applied immediately on both. The client's instance is `Pending` until its id appears in the bound active-effect ids (1.4+), then `Validated`. Both sides write their own id into that list, so the client normally promotes its instance on its next tick, before the server has answered; `ClientEffectApplicationTimeout` (0.5 s, `Public/Settings/GMASNetworkTimingSettings.h`) reaps only an instance still `Pending` and is **not** a rollback guarantee for a rejected prediction (below) |
| `PredictedQueued` | owner and server, anywhere | inside a move or the ancillary tick it is `Predicted`; outside, buffered locally and drained at the start of the next prediction or ancillary tick (each side keeps its own buffer) |
| `ServerAuth` | server only (a client call returns `false`) | the server reserves an id and queues a server operation; the owning client receives it by RPC and both sides apply it inside the client move that acknowledges it (grace force below) |
| `ServerInstantAttribute` (1.4+) | server only | applied in the current server tick, outside the move: the modifiers reach clients through the attribute's own replication. No client instance, no prediction, not in the move history. Only for `Instant` effects with no `Delay` and no `GrantedTags`, `GrantedAbilities`, `CancelAbilityOn*`, `EndAbilityOn*Query`, `ApplyEffectOnEnd`, `RemoveEffectOnEnd` (anything else falls back to `ServerAuth` with an Error; 1.4.0 checked only the type, tags and abilities). For what a player *suffers* (damage), never for an attribute predicted logic reads |
| `ClientAuth` (hidden) | whitelisted classes only (`ClientAuthorizedAbilityEffects`, 1.4+) | the client decides; its tags go to an unbound container (1.4+). Self-only cosmetics |

- `bUniqueByEffectTag` (1.4+, `FGMCAbilityEffectData`, `Public/Effects/GMCAbilityEffect.h`): a second apply with the same `EffectTag` is rejected while one is active; if the old one is already in its deferred end (reachable on 1.4.1+, where the grace deferral works), the new one replaces it: the server ends the old at once, the client suspends it and finalizes or revives it according to the successor's verdict (`Validated` / `Timeout`; a completed `Instant` successor ends the old at once on the client too). Prefer `Ticking` effects for exact client/server symmetry in that window.
- Modifier layering (1.4+): base layer (`Set` winner, else `RawValue`) → `AddPercentageOfBase` → `Add`. `SetReplace` also drops `Add` modifiers placed before it ("reset state" semantics); `Set` keeps them stacking.
- `ClientGraceTime` (`0` = `DefaultClientGraceTime`, 0.5 s) is the window in which both sides end a removed ticking or periodic effect on the same logical move. 1.4.1+: a networked removal (`RemoveActiveAbilityEffect`, `Private/Components/GMCAbilityComponent.cpp`) arms the same absolute `EndAtActionTimer = ActionTimer + grace` on each side, the effect keeps ticking until then and both sides apply the same number of ticks; a second removal inside the window changes nothing. Opt out with `DefaultClientGraceTime = 0` (each side ends at once; the drift returns, up to about a round trip); an effect's own `ClientGraceTime > 0` still defers it. On 1.4.0 a source-level override disabled the defer, so such removals drifted by about one round trip. Known limits on 1.4.1: a server-explicit removal reaches the client by `RPCClientEndEffect`, which arms the client's window at receipt (about one round trip after the server's), not at the server's end time.
- A server reject never rolls the client back by itself: the next correction restores the bound attributes, but the client's own instance is normally already `Validated` (its own id is in the list it polls) and keeps running until the server ends it, so `ClientEffectApplicationTimeout` rarely fires. Design abilities so the server agrees (same bound tags, same cost checks on GMC-bound attributes) instead of designing around rejection.

Recipes: `gmas:gmas-effect`.

## Server operations

Every server-originated operation on a pawn (a `ServerAuth` apply or remove, an activation on a server-controlled pawn, `AddImpulse`, `FireCustomEvent`, `SetActorLocation`) goes through one private funnel, `EnqueueServerOperation` (1.4+); `ShouldApplyServerOpImmediately` picks the path:

- **Target with an acknowledging client** (a remote player pawn, the listen host's own pawn, standalone): `QueueServerOperation(OperationID, Timeout)` (`Public/Utility/GMASBoundQueueV2.h`) with `Timeout = ServerOperationGraceSeconds` (`UGMASNetworkTimingSettings`, default 1 s, 1.4.1+; a fixed `Timeout = 1.0f` default on 1.4.0). The operation goes out by client RPC, the owning client puts it in the bound slot of its next move and both sides apply it in that move (the owner predicts it; replays restore it); the move acknowledges it. Without an acknowledgement within the grace window the server forces it on its side (`OnServerOperationForced` → `ProcessOperation(bForce = true)`) about **1 s** later, outside any move. A client operation the server never received is reported once as `[OperationLost]` (1.4.1+).
- **Target without an autonomous client** (AI, level-placed or unpossessed pawns on a networked server): queued with a zero grace and forced on that pawn's own next ancillary tick, about one frame later (1.4+; before, such pawns waited the full second). Never applied inline, because the call usually runs inside *another* pawn's move.

Consequences: a server operation lands in the target's move, not at the call site. An operation the owning client cannot apply (every candidate ability fails its tag checks there, or a batch payload is missing) is not acknowledged, so the server forces it about a second later and the two sides disagree until then. Design server-driven abilities so the first granted candidate passes on both sides, with `ActivationRequiredTags` / `ActivationBlockedTags` on bound tags both sides see.

## Impulses

`AddImpulse(FVector Impulse, bool bVelChange = false)` on the **target's** ASC, called on the server (a client call logs a warning and returns). It becomes a server operation; `ProcessOperation` applies it through `UGMC_MovementUtilityCmp::AddImpulse(const FVector&, bool)` (`Source/GMCCore/Public/Components/GMCMovementUtilityComponent.h`) inside the target's prediction tick, on the server and on the owning client in the same logical move; a replay applies it again at the same point. No idempotency guard is needed.

- **Replace velocity:** there is no "set velocity" operation; send `Desired − Current` with `bVelChange = true` (a velocity change, mass ignored).

```cpp
// Server only. Usually called from a hit handler inside the attacker's move; the
// operation itself is applied in the victim's next move, never here.
void AMyPawn::ServerKnockBack(AMyPawn* Victim, const FVector& DesiredVelocity)
{
    if (!HasAuthority() || !Victim) { return; }
    const FVector Current = Victim->GetVelocity();                  // the server's view of it
    Victim->AbilitySystem->AddImpulse(DesiredVelocity - Current, /*bVelChange=*/true);
}
```
- The owning client applies the operation about one round trip after the server computed it from the server's view of the velocity, so the owner's result drifts by whatever happened in between. Accept it for knock-backs; for an exact trajectory send the *parameters* with `FireCustomEvent(EventTag, Payload)` (1.4+) and integrate them locally on both sides.
- An impulse from the owner's own predicted logic (a dash) is not a server operation: write the velocity in the prediction tick on both sides.

## Tags and synced events

- **Bound state tags.** `ActiveTags` is a bound container (`ServerAuth_Output_ClientValidated`, sent to simulated proxies). State that predicted logic derives is set with `MatchTagToBool(Tag, bool)` **in the prediction tick, every tick** (`State.Stunned` from the stun timer): it adds or removes the tag so both sides agree move by move. `AddActiveTag` / `RemoveActiveTag` are the raw calls; outside the move they desynchronize. Effects grant tags the same way (`GrantedTags`).
- **Server-set tags outside the move.** `AddSynchronizedTag(Tag, AllowMultipleInstance)` / `RemoveSynchronizedTag(Tag, RemoveEveryInstance)` (1.4+, authority only) wrap an effect so the tag arrives through the bound queue; never mix them with a tag an effect already grants.
- **Queries.** `HasActiveTag` and friends answer from the union of bound and client-auth tags (1.4+); `HasBoundActiveTag` / `GetBoundActiveTags` see only the validated set. `ActivationRequiredTags` / `ActivationBlockedTags` are checked with `HasActiveTag`, so they read that union: a tag granted by a `ClientAuth` effect passes the gate on the client only. Put only bound tags in gates. `ActivationQuery` and modifier `Conditions` match the bound `ActiveTags` directly.
- **Change notifications.** `OnActiveTagsChanged` and `AddFilteredTagChangeDelegate` fire from the ancillary and simulation ticks: cosmetics and UI, not gameplay.
- **Synced events.** `FireCustomEvent(FGameplayTag EventTag, FInstancedStruct Payload)` (1.4+, server) delivers one tagged payload to the server and the owning client at the same logical move (`OnCustomEvent`); `SetActorLocation(FVector)` is the teleport form. `ExecuteSyncedEvent(FGMASSyncedEventContainer)` still exists, but on 1.4 it does nothing; it is deprecated in 1.4.1 (one Warning, removed in 1.5) together with `OnSyncedEvent`: do not build on it. Nothing in the bound queue reaches simulated proxies; a cue everyone must see is a cosmetic multicast (next section) or bound state they react to.

## Cosmetics

The ASC's FX helpers share one shape: `SpawnSound(Sound, Location, Volume, Pitch, bIsClientPredicted)`, `SpawnParticleSystemAtLocation(SpawnParams, UserParams (1.4+), bIsClientPredicted, bDelayByGMCSmoothing)`, `SpawnParticleSystemAttached(...)`, `SpawnParticleAtPoint(...)` (1.4+), `PlayCameraShakeAtLocation(...)` (1.4+), each with an unreliable `MC_*` multicast twin.

1. On the server the helper multicasts, then plays locally.
2. Each receiver skips when it `HasAuthority()` (already played) or when it is the locally controlled owner and `bIsClientPredicted` is true (it played on its own live move).
3. The owning client calls the same helper on its live move with `bIsClientPredicted = true` to see the cue at once; `bDelayByGMCSmoothing` lines the cue up with a simulated proxy's smoothing delay.

Predicted cosmetics run only on the live move (never on replayed or simulated executions) and never decide anything. The full table of what plays where is in `gmas:gmas-ability`.

## Identity and configuration

Values fixed for a pawn's life (team, archetype, loadout) need two deliveries:

- **initial-only actor replication** (`DOREPLIFETIME_CONDITION(..., COND_InitialOnly)` on the pawn or its player state), because bound values reach simulated proxies and the owning client late (`gmas:gmc-prediction`, smoothing);
- **bound seeding**: predicted logic reads bound values only, so every machine copies the identity into bound state (a bound byte for the archetype, attributes initialised from it in `SetAttributeInitialValue`) before the first move. `GetExternalModifierValue(ExternalTag, ValueIndex)` (1.4+) must return the same value on the server and the owning client and stay stable within a move, so derive it from that identity, never from anything replicated later.

Derive everything else (parameters, limits, costs) from the identity through data assets on each machine: one apply path, no second source of truth.

## Reading the GMAS headers

| Fact | File (under `Source/GMCAbilitySystem/`) |
|---|---|
| the five forwarders, `QueueAbility`, `MatchTagToBool`, tag queries, `ApplyAbilityEffect*`, `ApplyAbilityAttributeModifier`, `AddImpulse`, `FireCustomEvent`, FX helpers, `EGMCAbilityEffectQueueType`, `EGMCEffectAnswerState` | `Public/Components/GMCAbilityComponent.h` |
| `bActivateOnMovementTick`, `CooldownTime`, `ServerConfirmTimeout`, activation tag containers | `Public/Ability/GMCAbility.h` |
| `FGMCAbilityEffectData` (`EffectType`, `Duration`, `ClientGraceTime`, `GrantedTags`, `bUniqueByEffectTag`, `MustHaveTags`) | `Public/Effects/GMCAbilityEffect.h` |
| `FAttributeData`, `FAttribute` and modifier layering, `FAttributeClamp`, `FGMCAttributeModifier` / `EModifierType` | `Public/Attributes/GMCAttributesData.h`, `GMCAttributes.h`, `GMCAttributeClamp.h`, `GMCAttributeModifier.h` |
| operation structs, `QueueServerOperation` and its grace | `Public/Utility/GMASBoundQueueV2_Operations.h`, `GMASBoundQueueV2.h` |
| `DefaultClientGraceTime`, `ClientEffectApplicationTimeout`, `ServerOperationGraceSeconds` (1.4.1+) | `Public/Settings/GMASNetworkTimingSettings.h` |
| `ProcessOperation`, `EnqueueServerOperation`, `ShouldApplyServerOpImmediately`, `BindReplicationData` bodies | `Private/Components/GMCAbilityComponent.cpp` |

## Checklist

- The movement component forwards all five hooks; `AbilitySystem->BindReplicationData()` is the last line of `BindReplicationData_Implementation`, and `AttributeDataAssets` plus the starting lists are filled before it, identically on every machine.
- Every value in the "where does it live" table has exactly one home: nothing predicted logic reads is `Replicated` or an unbound attribute; nothing only the HUD reads is bound.
- Attributes: `bGMCBound` only for predicted reads; every clamp set deliberately (`Max` / `MaxAttributeTag`, or `bClampMax` off); changes through effects or `ApplyAbilityAttributeModifier` inside the move on both sides; no `SetAttributeValueByTag`.
- Abilities: activated through `QueueAbility`; `bActivateOnMovementTick` consistent per input tag; instances long-lived or guarded; cooldowns predicted logic reads are bound floats; side effects only on live moves, `CL_DoNotCombineNextMove()` after.
- Effects: queue type taken from the table; `ServerInstantAttribute` only for attribute-only server effects nobody predicts; `bUniqueByEffectTag` for single-instance effects; the grace deferral left on (or `DefaultClientGraceTime = 0` chosen knowingly); no design that relies on a rejected prediction rolling back.
- Server operations target the victim's ASC and are designed so the first candidate passes on both sides; AI targets rely on the next-tick force (1.4+).
- Impulses: `AddImpulse` on the target's ASC from the server; replace = `Desired − Current`, `bVelChange = true`.
- Tags: `MatchTagToBool` every prediction tick for derived state; `AddSynchronizedTag` for server-set tags; gates on bound tags.
- Cosmetics follow the multicast shape; identity rides initial-only replication and seeds bound state.
- Verified under networked PIE with a client and a bot (`gmas:gmas-testing`), not only standalone.
