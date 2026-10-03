---
name: gmas-effect
description: Use when creating or modifying a UGMCAbilityEffect or FGMCAbilityEffectData - duration and periodic settings, granted tags, modifiers and conditions, queue type, runtime-built effects, removal.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

An effect is the only sanctioned way to change what a `UGMC_AbilitySystemComponent` ("the ASC") owns: attributes, active tags, granted abilities. A `UGMCAbilityEffect` (`Public/Effects/GMCAbilityEffect.h`) is a class whose defaults carry one `FGMCAbilityEffectData`; applying it duplicates the class default object, initialises the copy with that data (or data you built at runtime) and registers it in the ASC's `ActiveEffects`. Active effects tick inside `GenPredictionTick` (`TickActiveEffects`, after the abilities, before `ProcessAttributes` recalculates attribute values) on every machine that runs the move, replays included. Which queue type reaches which machine and the server-operation funnel are in `gmas:gmas-rules`; modifier arithmetic and layering in `gmas:gmas-attribute`; costs and effects owned by an ability in `gmas:gmas-ability`. Seven worked effects: [references/recipes.md](references/recipes.md). GMAS paths below are relative to `Source/GMCAbilitySystem/`.

## Anatomy

`FGMCAbilityEffectData`, timing:

| Field | Default | Meaning |
|---|---|---|
| `EffectType` (`EGMASEffectType`) | `Instant` | `Instant`: modifiers once, ends in the same call. `Persistent`: modifiers once at start, lives until `Duration` or removal. `Ticking`: modifiers every prediction tick scaled by the tick's delta (values are per second). `Periodic`: the whole modifier value once per `PeriodicInterval` |
| `Duration` | `0` (infinite) | seconds from the start (after `Delay`); `EndEffect` fires in the tick where `ActionTimer >= StartTime + Duration`. Ignored by `Instant` |
| `Delay` | `0` | seconds before `StartEffect`: no tags, abilities or modifiers until then, though `TickEvent` already runs |
| `PeriodicInterval` | `1.0` (min `0.1`) | period in seconds, anchored on the start time: boundaries are detected statelessly from `ActionTimer`, so a replay recounts the same crossings (1.4+) |
| `bPeriodicFirstTick` | `true` | apply once at start, then at every boundary |
| `bNegateEffectAtEnd` | `false` | `Ticking` / `Persistent` / `Periodic` only: modifiers become temporal entries layered on `Value` and vanish at the end, instead of permanent writes to `RawValue` |
| `bReevaluateConditionsWhilePersistent` (1.4+) | `false` | `Persistent` with negate: re-resolve every modifier's `Conditions` each tick, dropping and re-adding its single entry, so a buff toggles with tags without re-applying |
| `PauseEffect` | empty | while the owner has any of these tags, no modifier applications; `Duration` keeps running and periodic boundaries crossed meanwhile are dropped (logged at `Verbose`, 1.4+) |
| `ClientGraceTime` | `0` (= project default) | the bilateral defer window for removing a `Ticking` / `Periodic` effect; see the queue-type section |

Tags:

| Field | Default | Meaning |
|---|---|---|
| `EffectTag` | none | identity for queries, removal by tag and uniqueness; optional but use it (`Effect.Buff.Haste`) |
| `GrantedTags` | empty | added to the bound `ActiveTags` at start, removed at end (to the unbound client-auth container for `ClientAuth` effects, 1.4+). Useless on `Instant` |
| `bPreserveGrantedTagsIfMultiple` | `true` (1.4+; `false` before) | at end, keep each granted tag that any other live effect still grants (1.4+; before, only a sibling with the same `EffectTag` counted). The containers are set-like, so without it the first of two granters to end strips the tag |
| `bUniqueByEffectTag` (1.4+) | `false` | reject the apply while an effect with the same exact `EffectTag` is alive; replace semantics in its deferred-end window, see below |
| `ApplicationMustHaveTags`, `ApplicationMustNotHaveTags` | empty | checked once at start: at least one of the must-have tags, none of the must-not |
| `MustHaveTags`, `MustNotHaveTags` | empty | the same test at start **and every tick**: failing ends the effect |
| `EffectDefinition`, `ActivationQuery`, `MustMaintainQuery`, `EndAbilityOnActivationQuery`, `EndAbilityOnEndQuery` | empty | `EffectDefinition` describes the effect for query-based removal (`RemoveEffectsByQuery`); the two queries gate start and life against the owner's tags; the two `EndAbility*` queries end active abilities whose definition tags match |

Abilities and chains:

| Field | Meaning |
|---|---|
| `GrantedAbilities` | ability tags granted at start (`GrantAbilityByTag`) and removed at end. Not refcounted: two effects granting the same tag lose it when the first ends |
| `CancelAbilityOnActivation`, `CancelAbilityOnEnd` | `EndAbilitiesByTag` on each entry (hierarchical match on the ability's `AbilityTag`) at start / at end |
| `ApplyEffectOnEnd`, `RemoveEffectOnEnd` (1.4+) | at end, apply these classes / remove every effect with these tags, `Predicted` inside a move or the ancillary tick (or standalone), otherwise `PredictedQueued` |

Modifiers: `Modifiers`, an array of `FGMCAttributeModifier` (next section). Networking and runtime (not editable): `bServerAuth` exempts a client instance from the confirmation timeout (the `ServerAuth` path marks its instances `Validated` instead; `AddSynchronizedTag` sets the flag); `bClientAuth` (1.4+) is set by the `ClientAuth` path; `EffectID`, `StartTime`, `EndTime`, `CurrentDuration`, `SourceAbilityComponent`, `OwnerAbilityComponent` are filled when the effect is applied.

Class hooks on `UGMCAbilityEffect` (all run inside the move, on the owner and the server, replays included):

| Hook | When | Typical use |
|---|---|---|
| `StartEffect()` (C++ virtual, protected) → `StartEffectEvent()` (BlueprintNativeEvent) | after the application checks, tags, abilities and the first modifiers | cosmetics on live moves, bookkeeping. `StartEffect` is the whole start: override the event, not it |
| `TickEvent(float DeltaTime)` ("Effect Tick", BlueprintNativeEvent) | every effect tick, before the ongoing tag checks and modifier application | polling a bound value, timers kept in `CurrentDuration` |
| `AttributeDynamicCondition()` ("Dynamic Condition", BlueprintNativeEvent, default `true`) | every tick, before applying modifiers | skip this tick's application (a drain only while moving) without ending the effect; periodic boundaries crossed while it is false are dropped |
| `PeriodTickEvent()` ("Period Tick", BlueprintNativeEvent) | once per tick in which a periodic boundary was crossed, after its modifiers; not for the `bPeriodicFirstTick` application | a per-period cue |
| `OnAttributeModifierApplication(const FGMCAttributeModifier&)` (C++ virtual, 1.4+) → `K2_OnAttributeModifierApplication` when `bCallOnAttributeModifierApplication` (class default, `false`) | after each `ApplyAbilityAttributeModifier` the effect makes, with the resolved copy (`MetaTags`, `ActionTimer`, `DeltaTime` filled) | damage-type reactions; the recalculated `Value` is visible only after `ProcessAttributes` later in the tick |
| `EndEffect()` (C++ virtual, BlueprintCallable) → `EndEffectEvent()` (BlueprintNativeEvent) | after temporal modifiers are dropped, abilities cancelled, tags and grants removed; then the chain fields run | cosmetics on live moves. Idempotent: `bCompleted` |

The ASC broadcasts `OnEffectApplied` / `OnEffectRemoved` (dynamic delegates) from the same points, replays included: presentation guards itself with `!IsReplayingForGMASLogic()` (1.4+; `CL_IsReplaying()` on the movement component before).

## Choosing the queue type

`EGMCAbilityEffectQueueType` (`Public/Components/GMCAbilityComponent.h`) is a parameter of the apply call, not of the effect. The full table of what each queue reaches is in `gmas:gmas-rules`; what matters when authoring:

| Queue type | Who calls | Who applies, when | What the client sees | On a server reject |
|---|---|---|---|---|
| `Predicted` | the same predicted logic on the owning client and the server, inside the move (or the ancillary tick; no runtime check on apply) | each machine at once; ids derive from `ActionTimer`, so both sides agree | its own instance immediately, `Pending` until the id shows up in the server's bound active-effect ids (1.4+), then `Validated` | after `ClientEffectApplicationTimeout` (0.5 s, `Public/Settings/GMASNetworkTimingSettings.h`; 1 s field on the component before 1.4) the instance is `Timeout` and ended; the replay purges its temporal entries; permanent writes settle with the next correction |
| `PredictedQueued` | anywhere | as `Predicted` inside a move or the ancillary tick; outside, buffered per machine in `PendingPredictedOperations` and drained at the start of the next prediction or ancillary tick | the same, one tick later. A buffered apply returns `true` with id `-1` and no instance: keep an `EffectTag` to find it | as `Predicted` |
| `ServerAuth` | server only (a client call returns `false` silently) | the server reserves an id in its own range, the owning client receives the operation by RPC and applies it in the move that carries it, the server applies it when it processes that move; no acknowledgement → forced on the server about 1 s later; targets without an autonomous client get it on their next ancillary tick (1.4+) | the instance about one round trip after the server call, already `Validated`; a replay re-processes the operation idempotently (1.4+) | not applicable |
| `ServerInstantAttribute` (1.4+) | server only (`Error` + `false` on a client) | immediately in the current server tick, outside the move; `Instant` effects whose class and data carry no `GrantedTags` / `GrantedAbilities`, otherwise `ensure` + `Warning` + fallback to `ServerAuth` | nothing: no client instance; the modifiers arrive through the attribute's own replication (an unbound attribute) | not applicable |
| `ClientAuth` (hidden) | the owning client, for classes in `ClientAuthorizedAbilityEffects` (1.4+; `Warning` + `false` otherwise) | the client at once (`Validated` on the spot) and the server through the bound queue; granted tags go to the unbound client-auth container | its own instance; nothing is validated | none |

`ServerAuthMove` still exists hidden and is treated as `ServerAuth` (1.4). Pick: predicted ability work (costs, self-buffs, drains the owner anticipates) → `Predicted`, or `PredictedQueued` from code that may run outside the move; anything one pawn does to another, and `StartingEffects` → `ServerAuth`; a one-shot on an unbound server stat (damage) → `ServerInstantAttribute`.

**`ClientGraceTime`.** Removing a `Ticking` or `Periodic` effect on a networked game is meant to arm `EndAtActionTimer = ActionTimer + grace` on both sides (`0` = `DefaultClientGraceTime`, 0.5 s) so each fires the same number of applications before ending (1.4+ meaning; on 1.3 the same field, default 1 s, was the RPC grace of a `ServerAuthMove` operation). **On the 1.4 tree the defer is disabled** by a source-level override in `RemoveActiveAbilityEffect` (`Private/Components/GMCAbilityComponent.cpp`: `const bool bHasGracePeriod = false;`): removal ends the effect at once on each side, `ClientGraceTime` and the project default change nothing, and a predicted removal of a time-driven effect can drift by about one round trip between client and server. Check that line in your tree before designing around the window.

## Modifiers and conditions

`FGMCAttributeModifier` (`Public/Attributes/GMCAttributeModifier.h`): `AttributeTag` (the target on the ASC the effect is applied to), `Op` (`EModifierType`, default `Add`), `ValueType` (`EGMCAttributeModifierType`: `AMT_Value` with `ModifierValue`; `AMT_Attribute` with `ValueAsAttribute`; `AMT_Custom` with `CustomModifierClass`; `AMT_External` (1.4+) with `ExternalTag` + `ExternalValueIndex`, resolved by the ASC's virtual `GetExternalModifierValue`), the op-specific `X` / `Y` / `XAsAttribute` / `YAsAttribute` / `XAttribute` / `YAttribute` / `Attributes`, `MetaTags` and `Conditions`. The op table, the value sources, percent conventions and the 1.4 layering order (`Set` base → `AddPercentageOfBase` → `Add`, `SetReplace` dropping earlier adds) are in `gmas:gmas-attribute`; do not restate them in the effect.

- **Where a modifier is applied.** `Instant` and `Persistent`: once, at start. `Periodic`: at start when `bPeriodicFirstTick`, then once per boundary (several boundaries inside one tick give several applications). `Ticking`: every tick, `DeltaTime` scaled. Each application stamps the copy with the effect, `ActionTimer`, its index and the delta (`InitModifier`), resolves `Conditions`, then calls `ApplyAbilityAttributeModifier`.
- **Permanent or temporal.** Without `bNegateEffectAtEnd` (and always for `Instant`) the modifier writes `RawValue`: a cost, damage, regeneration. With it, every application registers a temporal entry that `Value` layers and `EndEffect` removes; a `Ticking` drain with negate refunds everything at the end.
- **`Conditions` (1.4+, `FGMCModifierCondition`).** Ordered rules, each an `FGameplayTagQuery` matched against the owner's **bound** active tags only (client-auth tags are ignored, so the result replays identically); the first matching rule wins: `Skip` drops this application, `OverrideValue` swaps the value source (its own `ValueType` and payload). An empty query never matches. `CanAffordAbilityCost` resolves them too: a skipped cost is free.
- **`MetaTags`** ride on the modifier copy (`Damage.Fire`): GMAS does not interpret them; read them in `OnAttributeModifierApplication` or a custom calculator.
- **Which attribute.** A `Predicted` effect must modify bound attributes (an unbound attribute ignores the client's apply); a `ServerInstantAttribute` effect must modify unbound ones (a bound `RawValue` written outside the move corrects every client). Decide with `gmas:gmas-rules`.

## Lifecycle rules

1. **Apply** (`ApplyAbilityEffect`): with `bUniqueByEffectTag`, an alive effect with the same exact tag rejects the apply (`false`, `Verbose` log) before anything else happens. The copy gets its id (`ActionTimer × 100`, bumped past ids in use; `-1` with an error when `ActionTimer` is 0, as on a smoothed listen-server pawn), lands in `ActiveEffects` and in the bound active-effect ids (1.4+), then `InitializeEffect` sets `StartTime = ActionTimer + Delay` and `EndTime = StartTime + Duration`. `Delay == 0` starts it inside the same call.
2. **Start** (`StartEffect`): application tags and `ActivationQuery` are tested (against the union of bound and client-auth tags). A failure calls `EndEffect` on an effect that never applied anything: no tags, no `OnEffectRemoved`, yet the apply call still returned `true`; check `bCompleted` on the instance when it matters. Otherwise: tags, abilities, `CancelAbilityOnActivation`, `EndAbilityOnActivationQuery`, `OnEffectApplied`, the first modifiers, and for `Instant` an immediate `EndEffect` (its tags are granted and removed in the same call; `StartEffectEvent` runs afterwards).
3. **Tick** (every prediction tick, `Tick(DeltaTime)`): `CurrentDuration` updated, `TickEvent`, ongoing `MustHaveTags` / `MustNotHaveTags` / `MustMaintainQuery` (a failure ends the effect this tick, before any application), then the type-specific application unless paused, not started, or the dynamic condition is false; finally `CheckState` starts a delayed effect at `StartTime` and ends a timed one at `EndTime`.
4. **End** (`EndEffect`): idempotent; temporal entries removed per modifier index; `EndAbilityOnEndQuery`, `CancelAbilityOnEnd`, granted tags removed with the preserve rule, granted abilities removed, `OnEffectRemoved`, `EndEffectEvent`, chain fields. The instance stays in `ActiveEffects` as `bCompleted` until the next `TickActiveEffects` pass removes it (the server also sends a redundant `RPCClientEndEffect`): queries can return a completed instance for one tick.
5. **Unique replace (1.4+).** When the same-tag match is in its deferred-end window (`EndAtActionTimer` armed), the new apply succeeds and replaces it: the server ends the old one at once; the client suspends it (`bPendingDeathBySuccessor`: no modifier applications, tags kept) and finalizes it when the successor is `Validated` or revives it on `Timeout`. With the 1.4 grace defer disabled nothing arms that window, so on the shipped tree `bUniqueByEffectTag` is plain rejection; to refresh a unique buff, remove it by tag and apply again in the same tick (a removed instance is `bCompleted` and no longer counts). Prefer `Ticking` effects where exact client/server symmetry matters in the replace window.
6. **Replay.** Effect instances are not rewound: a replay re-runs their ticks against the restored bound attributes and purges temporal entries newer than the restored move; periodic boundaries are recounted from `ActionTimer` (1.4+); server operations are re-processed idempotently (1.4+). A `Predicted` instance that ended inside the replay window is not recreated (`gmas:gmas-rules`), so keep an effect's observable state in bound attributes and tags, never in members of the effect.

## Applying and removing at runtime

| Call (`Public/Components/GMCAbilityComponent.h`) | Returns | Notes |
|---|---|---|
| `ApplyAbilityEffectSafe(EffectClass, InitializationData, QueueType, bool& OutSuccess, int& OutEffectHandle, int& OutEffectId, UGMCAbilityEffect*& OutEffect, UGMCAbility* HandlingAbility = nullptr)` | via out params | Blueprint "Apply Ability Effect". Data that `IsValid()` (modifiers, tags, must-have tags, `bUniqueByEffectTag` or an `EffectTag`) is used as the whole effect; an empty struct means the class defaults. `HandlingAbility` registers the id (`DeclareEffect`) so the ability removes the effect when it ends (by id, or by its cached `EffectTag` if a replay renumbered it) |
| `ApplyAbilityEffectShort(EffectClass, QueueType, HandlingAbility = nullptr)` | the instance or `nullptr` | class defaults, fire and forget |
| `ApplyAbilityEffect(EffectClass, InitializationData, QueueType, int& OutEffectHandle, int& OutEffectId, UGMCAbilityEffect*& OutEffect)` | `bool` | the C++ entry; **no fallback to the class defaults**: pass a complete struct. `OutEffectHandle` is a deprecated alias of `OutEffectId` |
| `ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData)` | the instance or `nullptr` | the inner path on an instance you created (`CommitAbilityCost` and the specs use it): uniqueness check, init, registration; no queue type, so only from predicted logic |
| `UGMCAbilityEffect::GetDefaultEffectData(EffectClass)` (static, 1.4+) | a copy of the class's `EffectData` | the start of every runtime-built effect |

```cpp
// Server hit handler: the victim's ASC, damage from the weapon, class defaults for the rest (1.4+).
void AMyPawn::ServerApplyDamage(UGMC_AbilitySystemComponent* VictimASC, float Damage)
{
    if (!HasAuthority() || !VictimASC) { return; }
    FGMCAbilityEffectData Data = UGMCAbilityEffect::GetDefaultEffectData(UMyEffect_Damage::StaticClass());
    Data.Modifiers[0].ModifierValue = -Damage;                       // Attribute.Health, Add, AMT_Value
    int Handle = -1, Id = -1; UGMCAbilityEffect* Instance = nullptr;
    VictimASC->ApplyAbilityEffect(UMyEffect_Damage::StaticClass(), Data,
        EGMCAbilityEffectQueueType::ServerInstantAttribute, Handle, Id, Instance);
}
```

Keep `OutEffectId` (the handle is the same number) or the `EffectTag` for removal. Removal takes the queue type the apply took: a `Predicted` removal outside a move on a networked game is refused with an `ensure`; `ServerAuth` removals become server operations; `ServerInstantAttribute` removes at once on the server.

| Removal | Matches |
|---|---|
| `RemoveActiveAbilityEffectSafe(Effect, QueueType = Predicted)` | one instance |
| `RemoveEffectByIdSafe(TArray<int> Ids, QueueType = Predicted)` → `bool` | ids from the apply |
| `RemoveActiveAbilityEffectByTag(const FGameplayTag& Tag, QueueType = Predicted, bAllInstance = false)` | hierarchical `EffectTag` match; the first instance unless `bAllInstance` |
| `RemoveEffectByTagSafe(InEffectTag, NumToRemove = -1, QueueType = Predicted)` → count | exact `EffectTag` match; `-1` removes all |
| `RemoveEffectsByQuery(const FGameplayTagQuery& Query, QueueType)` → count | `EffectDefinition` containers |
| `RemoveActiveAbilityEffect(Effect)` | immediate `EndEffect`, no queue, no checks: internal paths only |

`RemoveActiveAbilityEffectByHandle`, `RemoveEffectByHandle`, `RemoveEffectByTag` and `RemoveEffectById` are deprecated forwarders. Query with `GetEffectById(Id)`, `GetEffectsByIds`, `GetActiveEffectsByTag(Tag, bMatchExact = true)`, `GetFirstActiveEffectByTag(Tag)` (hierarchical), `GetNumEffectByTag(Tag)` (exact), `GetActiveEffects()` (a copy of the id map); `GetEffectData()`, `GetCurrentDuration()`, `GetEffectRemainingDuration()` on the instance. `AddSynchronizedTag` / `RemoveSynchronizedTag` (1.4+) wrap a `Persistent` `ServerAuth` effect around one tag: never remove such a tag through another effect's `RemoveEffectOnEnd`.

## Checklist

- `EffectType`, `Duration`, `PeriodicInterval` and `bPeriodicFirstTick` chosen deliberately; `bNegateEffectAtEnd` on for anything that must be undone (buffs, stuns' modifiers), off for anything that must stay (costs, damage, regeneration).
- Every effect has an `EffectTag`; single-instance effects use `bUniqueByEffectTag` (1.4+) and know that on the 1.4 tree a refresh is remove-then-apply.
- The queue type matches the caller: `Predicted` / `PredictedQueued` from logic both sides run, `ServerAuth` for what one pawn does to another and for starting effects, `ServerInstantAttribute` (1.4+) only for `Instant` attribute-only effects on unbound attributes; nothing in a `ServerInstantAttribute` effect grants tags or abilities.
- Modifiers of predicted effects target bound attributes; modifiers of server-instant effects target unbound ones; `Conditions` use bound tags; `AMT_External` values are identical on the server and the owning client.
- Granted tags that abilities gate on live in `GrantedTags` (bound); `MustHaveTags` / `MustNotHaveTags` are understood as any-of; shared granted tags rely on `bPreserveGrantedTagsIfMultiple`; shared granted abilities are not refcounted.
- Runtime-built data starts from `GetDefaultEffectData` (1.4+) or sets every field; the ids or the tag are kept for removal; removal uses the apply's queue type, inside a move for `Predicted`.
- Hooks hold no gameplay state: `StartEffectEvent` / `EndEffectEvent` cosmetics run on live moves only; nothing reads `ClientGraceTime` as a guarantee on a tree where `bHasGracePeriod` is forced off.
- Verified under networked PIE with a client (`gmas:gmas-testing`): the effect appears on both sides in the Gameplay Debugger `GMCAbilitySystem` category, no `Not Confirmed By Server` errors, no corrections when it starts or ends.

## Which test to write

A spec in the style of `Source/GMCAbilitySystem/Private/Tests/GMAS_EffectSpec.cpp` (`GMAS.Unit.Effect`) and `GMAS_DurationSpec.cpp` (`GMAS.Unit.Duration`): a stub movement component (an empty `UGMC_MovementUtilityCmp` subclass created with `NewObject` and no owner, which reports standalone), a `UGMC_AbilitySystemComponent` with `GMCMovementComponent` set, a `UGMCAttributesData` carrying your real rows, `BindReplicationData()`, then `ActionTimer` seeded. Apply your class with `ApplyAbilityEffect(Class, Data, Predicted, Handle, Id, Instance)` (or the inner overload on a `NewObject<UMyEffect_Burn>`), set `ActionTimer` forward and call `TickActiveEffects(DeltaTime)` plus `ProcessAttributes(true)` for each simulated move (`GenPredictionTick` would overwrite `ActionTimer` from the stub's move timestamp), and assert after each step: `GetAttributeValueByTag` and `GetAttributeRawValue` after N ticks or periods, `HasActiveTag` for the granted tags, the count from `GetNumEffectByTag`; then remove through `RemoveEffectByIdSafe` and assert the negate restored `Value` while `RawValue` kept the permanent part, and that the tag is gone. Set `Data.bServerAuth = true` when the test jumps the timer past `ClientEffectApplicationTimeout`, as the duration spec does. Prediction, confirmation and replay need networked PIE (`gmas:gmas-testing`).
