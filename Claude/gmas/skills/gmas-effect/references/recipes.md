# Effect recipes

Seven `UGMCAbilityEffect` classes as class defaults (`EffectData`), each listing only the fields that leave their defaults, with the queue type the apply call takes. Field meanings, hooks and the apply / remove API are in [SKILL.md](../SKILL.md); modifier math in `gmas:gmas-attribute`; the attributes used here are `Attribute.Stamina` (bound, clamped by `Attribute.MaxStamina`) and `Attribute.Health` (unbound, server-owned) unless stated otherwise. Values are placeholders.

## 1. Cost: `UMyEffect_DashCost`

| Field | Value |
|---|---|
| `EffectType` | `Instant` (default) |
| `EffectTag` | `Effect.Cost.Dash` |
| `Modifiers` | `Attribute.Stamina`, `Op = Add`, `ValueType = AMT_Value`, `ModifierValue = -20` |

Applied by the ability, not by you: set it as `UMyAbility_Dash::AbilityCost` and call `CommitAbilityCost()` (or `CommitAbilityCostAndCooldown()`) from `BeginAbilityEvent`; the ability duplicates the class, copies its data and applies it through the inner `ApplyAbilityEffect(Effect, Data)` inside its own tick, so the spend is predicted on the owner and re-run by the server in the same move. Gate the activation yourself with `CanAffordAbilityCost()` in `PreExecuteCheckEvent`: it sums `Value + modifier` for every modifier of the cost class (conditions resolved, so a `Skip` rule makes the dash free while its tag is present) and refuses when any would go below 0; nothing calls it for you. `Instant` writes `RawValue`, the floor clamp keeps stamina at 0 or above, and `bNegateEffectAtEnd` stays off: a cost is never refunded by ending. For a held ability use a `Ticking` cost (`-20` per second) as `AbilityCost`, check `CanAffordAbilityCost(DeltaTime)` each tick and call `RemoveAbilityCost()` from the end event; the one instance the ability keeps (`AbilityCostInstance`) is what that removes. The stamina row is bound because this very check reads it in the move.

## 2. Over time: `UMyEffect_Regen`

| Field | Value |
|---|---|
| `EffectType` | `Periodic` |
| `Duration` | `6` |
| `Delay` | `2` |
| `PeriodicInterval` | `1` |
| `bPeriodicFirstTick` | `false` |
| `EffectTag` | `Effect.Regen` |
| `GrantedTags` | `State.Regenerating` |
| `PauseEffect` | `State.Sprinting` |
| `Modifiers` | `Attribute.Stamina`, `Add`, `AMT_Value`, `+15` |

Applied `Predicted` from the ability or movement logic that both sides run (or listed in the ASC's `StartingEffects` for a permanent regeneration, which the server applies as `ServerAuth`). Nothing happens for two seconds (the tag and the schedule start at `StartTime = apply + Delay`), then `+15` lands on every whole second of the schedule until `StartTime + Duration`; with `bPeriodicFirstTick` on, one more application lands at the start. Boundaries are counted from `ActionTimer` without per-effect state, so a replay recounts them and client and server agree; boundaries that fall while the owner carries `State.Sprinting` are dropped, not deferred (the schedule stays anchored). `bNegateEffectAtEnd` stays off: regenerated stamina is permanent. The same shape is a damage-over-time effect on `Attribute.Health` applied `ServerAuth` from the server's hit code (recipe 6): the client instance still runs for its tags while the unbound attribute only changes on the server. A `Ticking` variant (`+15` per second, scaled by each tick's delta) gives a smooth drain or regeneration instead of steps.

## 3. Timed buff with negate: `UMyEffect_Haste`

| Field | Value |
|---|---|
| `EffectType` | `Persistent` |
| `Duration` | `8` |
| `bNegateEffectAtEnd` | `true` |
| `EffectTag` | `Effect.Buff.Haste` |
| `GrantedTags` | `State.Hasted` |
| `Modifiers` | `Attribute.MaxStamina`, `Add`, `AMT_Value`, `+50` |

Applied `Predicted` from the ability that grants it (pass the ability as `HandlingAbility` when the buff must die with the ability). The modifier is registered as a temporal entry rather than written to `RawValue`, so `Value` reads `+50` while the effect lives and returns to the permanent part the tick the effect ends (at `EndTime`, on removal, or when `MustHaveTags` stop holding); a stamina row clamped by `Attribute.MaxStamina` sees the higher ceiling at once and is clamped back down afterwards. Several instances stack additively in application order; with `bPreserveGrantedTagsIfMultiple` (the 1.4 default) `State.Hasted` survives until the last one ends. For a buff that must switch itself on and off with a tag while it lives, add a `Conditions` rule (`Skip` while `State.Exhausted`) and set `bReevaluateConditionsWhilePersistent` (1.4+), which needs the negate flag.

## 4. Stun: `UMyEffect_Stun`

| Field | Value |
|---|---|
| `EffectType` | `Persistent` |
| `Duration` | `2` |
| `EffectTag` | `Effect.Stun` |
| `GrantedTags` | `State.Stunned` |
| `CancelAbilityOnActivation` | `Ability.Dash`, `Ability.Attack` |

No modifiers: the effect is its tag. Applied `ServerAuth` on the victim's ASC from the server code that decided the hit; the owning client receives it inside its next move and both sides carry `State.Stunned` in the bound `ActiveTags` for two seconds. Every ability the stun must block lists `State.Stunned` in `ActivationBlockedTags`, which both sides evaluate on the bound container, so an activation queued while stunned fails identically on the client and the server; abilities already running when it lands are ended through `CancelAbilityOnActivation` (hierarchical match on their `AbilityTag`). Movement reads the tag with `HasBoundActiveTag` inside the prediction tick. Two stuns overlapping share one tag entry: the preserve rule keeps it until the longer one ends. A stun the owner's own predicted logic derives from bound state (a stun timer the movement component binds) is set with `MatchTagToBool` instead and needs no effect (`gmas:gmas-rules`).

## 5. Unique-replace buff: `UMyEffect_Shield`

| Field | Value |
|---|---|
| `EffectType` | `Persistent` |
| `Duration` | `6` |
| `bNegateEffectAtEnd` | `true` |
| `EffectTag` | `Effect.Buff.Shield` |
| `bUniqueByEffectTag` (1.4+) | `true` |
| `GrantedTags` | `State.Shielded` |
| `Modifiers` | `Attribute.Armor` (bound), `Add`, `AMT_Value`, `+30` |

Applied `Predicted`. While one instance is alive, a second apply with the same exact `EffectTag` is rejected before it touches anything (`OutSuccess` false, a `Verbose` log), so the buff never stacks. The replace half of the feature only engages for an instance already in its deferred-end window, and the 1.4 tree never arms that window (the grace defer is forced off in `RemoveActiveAbilityEffect`): to refresh the shield, call `RemoveEffectByTagSafe(Effect.Buff.Shield, -1, Predicted)` and apply again in the same tick, which works because the removed instance is `bCompleted` and no longer counts as alive. On a tree with the defer active, an apply during the window replaces the old instance: the server ends it at once, the client suspends it and finalizes or revives it on the server's verdict. Keep the data `IsValid()` consistent: a runtime struct that only sets `EffectTag` and `bUniqueByEffectTag` is treated as a complete effect and loses the class's modifiers; start from `GetDefaultEffectData` (1.4+).

## 6. Server-authoritative damage: `UMyEffect_Damage`

| Field | Value |
|---|---|
| `EffectType` | `Instant` (default) |
| `EffectTag` | `Effect.Damage` |
| `Modifiers` | `Attribute.Health`, `Add`, `AMT_Value`, `-25`, `MetaTags = Damage.Kinetic`, `Conditions = [Skip while State.Invulnerable]` (1.4+) |

Applied from the server's hit code on the victim's ASC, never on a client. `ServerAuth` queues a server operation: the victim's owning client gets the instance inside its next move and acknowledges it, the server applies it when it processes that move (about one round trip), or forces it after about a second; for an AI victim the operation lands on its next ancillary tick (1.4+). Because `Attribute.Health` is unbound, the client instance changes nothing locally and the new value arrives through the attribute's replication; `OnAttributeChanged` fires there for the HUD. When the effect is attribute-only (no `GrantedTags`, no `GrantedAbilities`), `ServerInstantAttribute` (1.4+) skips the queue and applies in the current server tick, which is the better choice for raw damage at low server tick rates; add a `State.Hit` granted tag or a `CancelAbilityOnActivation` and the apply falls back to `ServerAuth` with a warning. The amount comes from the weapon at runtime: copy `GetDefaultEffectData(UMyEffect_Damage::StaticClass())`, set `Modifiers[0].ModifierValue`, and pass the struct (the C++ `ApplyAbilityEffect` takes it as the whole effect). The `Skip` rule reads the server's bound tags, so an invulnerability granted by a predicted effect protects on the server too; `MetaTags` reach `OnAttributeModifierApplication` (with `bCallOnAttributeModifierApplication`) for damage-type reactions.

## 7. External-value modifier: `UMyEffect_LoadoutStats`

| Field | Value |
|---|---|
| `EffectType` | `Persistent` |
| `Duration` | `0` (infinite, default) |
| `bNegateEffectAtEnd` | `true` |
| `EffectTag` | `Effect.Loadout` |
| `bUniqueByEffectTag` (1.4+) | `true` |
| `Modifiers` | `Attribute.MaxStamina`, `Add`, `ValueType = AMT_External` (1.4+), `ExternalTag = Loadout.Stamina`, `ExternalValueIndex = 0` |

The modifier's value is whatever the ASC answers for the key: override `GetExternalModifierValue(const FGameplayTag& ExternalTag, int32 ValueIndex) const` on your `UGMC_AbilitySystemComponent` subclass (it returns `0` until you do) and return the loadout's number from identity every machine already holds (an archetype id replicated initial-only and seeded into bound state, `gmas:gmas-rules`); the answer must be the same on the server and the owning client and must not change inside a move. Applied `Predicted` from the first prediction tick after spawn, or from `StartingEffects`. The value is read when the modifier is applied, not continuously: when the loadout changes, remove by tag and apply again (the unique flag keeps a double apply from stacking). With the negate flag the bonus is a temporal entry, so swapping loadouts leaves `RawValue` untouched; without it the first apply would bake the bonus in permanently.

```cpp
float UMyAbilitySystemComponent::GetExternalModifierValue(const FGameplayTag& ExternalTag, int32 ValueIndex) const
{
    if (ExternalTag == TAG_Loadout_Stamina && LoadoutData) { return LoadoutData->StaminaBonus; } // identical on both sides
    return Super::GetExternalModifierValue(ExternalTag, ValueIndex);
}
```
