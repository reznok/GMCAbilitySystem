---
name: gmas-attribute
description: Use when adding or changing GMAS attributes - GMCAttributesData rows, clamps, GMC-bound versus replicated, reading and modifying values at runtime, custom modifier calculators.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

An attribute is one float per tag (`Attribute.Stamina`) owned by a `UGMC_AbilitySystemComponent` ("the ASC"). It is declared as a row of a `UGMCAttributesData` asset, becomes an `FAttribute` when the ASC binds, and changes through effect modifiers. Whether a value should be bound, replicated or derived at all is decided in `gmas:gmas-rules`; the asset wiring on the ASC is in `gmas:gmas-setup`; the move cycle bound values ride is `gmas:gmc-prediction`. GMAS paths below are relative to `Source/GMCAbilitySystem/`.

## Anatomy

`FAttributeData` (`Public/Attributes/GMCAttributesData.h`), one row per attribute in the asset's `AttributeData` array:

| Field | Meaning | Default |
|---|---|---|
| `AttributeTag` | the identity; the picker enforces `Attribute.*` | – |
| `DefaultValue` | the starting value (clamped); hidden in the editor while `bStartFull` is on | `0` |
| `bStartFull` (1.4+) | start at the resolved upper clamp instead of `DefaultValue`; needs `bClampMax` with `Max` or `MaxAttributeTag`, otherwise ignored | `false` |
| `Clamp` | an `FAttributeClamp`, below | holds the value at 0 |
| `bGMCBound` | bind `Value` and `RawValue` into the GMC move (predicted), or replicate them (never predicted) | `true` |
| `ValueCombineMode` (1.4+) | `EGMC_CombineMode` of both binds | `CombineIfUnchanged` |

`FAttributeClamp` (`Public/Attributes/GMCAttributeClamp.h`); `ClampValue` runs at initialisation, on `RawValue` at every permanent change, and on `Value` after every layer of a recalculation (`Private/Attributes/GMCAttributeClamp.cpp`, `GMCAttributes.cpp`):

| Field | Meaning | Default |
|---|---|---|
| `bClampMin`, `bClampMax` (1.4+) | whether that bound exists; off ignores the literal and the tag | `true` |
| `Min`, `Max` | literal bounds | `0`, `0` |
| `MinAttributeTag`, `MaxAttributeTag` | another attribute of the same ASC whose current `Value` (modifiers included) replaces the literal; wins when set | empty |

**The default clamp is [0, 0] and active (1.4+): a row left with it never leaves 0.** For every row set `Max` or `MaxAttributeTag` (`Attribute.MaxStamina`) or untick `bClampMax`; same for the floor. Trees older than 1.4 have no flags and ignored an all-zero clamp (`IsSet()`), so rows that relied on that pin at 0 after an upgrade (`gmas:gmas-upgrade`); a deliberate `[0, 0]` pin is now expressible. 1.4.1+: `InstantiateAttributes` logs an Error naming the asset and tag for every row that resolves to an active `[0, 0]` clamp (a deliberate pin logs it too).

The runtime struct is `FAttribute` (`Public/Attributes/GMCAttributes.h`):

| Member | Meaning |
|---|---|
| `InitialValue` | the resolved start: `DefaultValue`, a `SetAttributeInitialValue` override, or the upper clamp for `bStartFull` |
| `RawValue` | the permanent part: `InitialValue` plus every modifier not registered in history (instant effects, direct calls, persistent effects without `bNegateEffectAtEnd`). Bound or replicated |
| `Value` | what gameplay reads: `RawValue` layered with the active temporal entries, clamped. Bound or replicated |
| `ValueTemporalModifiers` | private history of temporal entries (`FAttributeTemporaryModifier`: value, `ActionTimer`, kind, instigating effect). Never replicated: each side rebuilds it from its own effect ticks and a replay purges entries newer than the restored move |
| `Clamp`, `bIsGMCBound`, `bStartFull`, `ValueCombineMode` | copies of the row |
| `OnAttributeChanged` | 1.4.0 only: an `FAttributeChanged` delegate on the struct that nothing broadcast; removed in 1.4.1. Bind the ASC's delegate |

Bound attributes live in the ASC's `BoundAttributes` (sorted by tag), unbound ones in `UnBoundAttributes` (a fast array, `ReplicatedUsing = OnRep_UnBoundAttributes`).

## Bound or not

`bGMCBound = true` binds `Value` and `RawValue` as two `ServerAuth_Output_ClientValidated`, `Periodic_Output` floats with the row's combine mode (`BindReplicationData`, `Private/Components/GMCAbilityComponent.cpp`). Both sides compute them inside the move, a mismatch corrects and replays the client, simulated proxies receive them through smoothing. The price: bytes in every move and state, and with `CombineIfUnchanged` a fresh move every time the value changes.

Whether a value is bound, replicated or derived is decided by the "where does this state live" table in `gmas:gmas-rules`; for attributes that means bound only when predicted logic reads it in a move (a cost, a cap, a drain, and the `MaxAttributeTag` of a bound attribute), unbound when only the server changes it and other machines display it.

An unbound attribute cannot take part in prediction: `ApplyAbilityAttributeModifier` returns at once on a client for it, the server's `Value` and `RawValue` replicate through `UnBoundAttributes`, and the client-side history stays empty. Change it on the server only (`ServerAuth` or `ServerInstantAttribute` effects, `gmas:gmas-effect`). Never make it something predicted logic reads, and never bind something only the HUD reads (`gmas:gmas-rules`).

`ValueCombineMode` (1.4+): leave `CombineIfUnchanged`, so a discrete change (a dash cost) lands on its own move boundary and replays cleanly. `AlwaysCombineOverwrite` only for an attribute that changes every prediction tick (continuous regeneration) **and** feeds nothing in the movement integration: a combined move re-runs over the summed delta with the end value, so a speed or friction attribute would diverge from the server and correct. Judge such a change by corrections (`gmc.ShowClientCorrections`, `gmas:gmas-debug`), not by server frame rate.

## Declaring

- **Tags.** `Attribute.<Name>`; a cap is its own attribute (`Attribute.MaxStamina`) referenced by `MaxAttributeTag`. Each tag appears once across all assets of an ASC. 1.4.1+: a duplicate (or a row without a tag) logs an Error naming the asset(s) and the second declaration is ignored; on 1.4.0 nothing checked and a duplicate resolved silently (`GetAttributeByTag` searches the unbound set first).
- **Assets.** One or more `UGMCAttributesData` in the ASC's `AttributeDataAssets` (`gmas:gmas-setup`). Several assets are additive: a base set for every pawn plus one per archetype. The same list on every machine.
- **Before the bind.** `BindReplicationData()` calls `InstantiateAttributes()` and then binds the rows; the pawn's `PostInitializeComponents` triggers the bind through `Super`. Fill `AttributeDataAssets` in class defaults, in the constructor, or in `PostInitializeComponents` before `Super::PostInitializeComponents()`. Rows added later exist nowhere.
- **Initial values** (1.4.1+). `InstantiateAttributes` creates every row with its `DefaultValue` and clamp and resolves them (so `bStartFull` and a `MaxAttributeTag` declared later work), then calls the virtual `SetAttributeInitialValue(const FGameplayTag&, float& BaseValue)` (and the Blueprint event `OnInitializeAttributeInitialValue`) once per attribute, then settles every row again so dependent rows see the overrides. The override wins over `bStartFull` but never over the clamp (out of range: an Error and the clamped value); a value equal to the resolved default counts as untouched; dependencies resolve one level deep (a row reading a second row that itself depends on an overridden third row sees the second row's pre-override value). Override it to seed from identity already identical on both sides (an archetype). `GetAttributeInitialValueByTag` returns the value actually applied (`-1` and one Warning per tag for an unknown tag). On 1.4.0 `bStartFull` won over the override and the getter returned the row's `DefaultValue`.

```cpp
// Rows chosen at runtime: inject before Super runs the bind, from data every machine has.
void AMyPawn::PostInitializeComponents()
{
    if (ArchetypeAttributes) { AbilitySystemComponent->AttributeDataAssets.AddUnique(ArchetypeAttributes); }
    Super::PostInitializeComponents();
}
```

Example rows: `Attribute.MaxStamina`, `DefaultValue 100`, `Clamp.Max 1000`, bound; `Attribute.Stamina`, `bStartFull`, `Clamp.MaxAttributeTag = Attribute.MaxStamina`, bound.

## Reading and changing at runtime

| Call (`Public/Components/GMCAbilityComponent.h`) | Returns |
|---|---|
| `GetAttributeValueByTag(Tag)` | `Value`: after modifiers, clamped; `0` for an unknown tag |
| `GetAttributeRawValue(Tag)` | `RawValue`: the permanent part; `0` for an unknown tag |
| `GetAttributeByTag(Tag)` | `const FAttribute*` (null when unknown): every member |
| `GetAllAttributes()` | pointers to every attribute, unbound first |
| `GetAttributeClampByTag(Tag)` | a copy of the clamp |

**Change through effects** (`gmas:gmas-effect`). Every modifier of an effect reaches the attribute through `ApplyAbilityAttributeModifier(const FGMCAttributeModifier&)`; the effect stamps the instigator, `ActionTimer`, application index and delta time (`InitModifier`) and resolves the modifier's `Conditions` first. An `Instant` effect, and any effect without `bNegateEffectAtEnd`, writes `RawValue`; a `Ticking`, `Periodic` or `Persistent` effect with `bNegateEffectAtEnd` registers temporal entries that `Value` layers and that vanish when the effect ends or a replay purges them. A replay does not re-create an effect instance that already ended (`gmas:gmas-rules`), so a permanent write made by an `Instant` effect inside the replay window is not reproduced by it (the source notes that permanent modifiers are not replay-safe by construction): the client carries the restored `RawValue` until the server state that contains the write corrects it. A direct call from predicted logic, below, is re-run by the replay and needs no such settling.

**Change directly from predicted logic.** `ApplyAbilityAttributeModifier` is public. A modifier you build yourself takes the permanent path (`bRegisterInHistory` defaults to `false`): `RawValue += ModifierValue` (`DeltaTime` defaults to `1`), clamped. Because `RawValue` is bound, a replay restores it and re-runs the call, so the write is replay-safe when it runs inside the prediction tick on the owning client and the server alike, from bound inputs only (`gmas:gmc-prediction`). `Value` is recomputed by the ASC's `GenPredictionTick` (`ProcessAttributes`): apply before forwarding that tick, or read `GetAttributeRawValue` in the same tick.

```cpp
// In UMyMovementCmp::GenPredictionTick_Implementation, before AbilitySystem->GenPredictionTick(DeltaTime).
// Both sides run it from the bound input; a replay runs it again on the restored RawValue.
if (bInput_Dash && AbilitySystem->GetAttributeValueByTag(StaminaTag) >= DashCost)
{
    FGMCAttributeModifier Spend;
    Spend.AttributeTag  = StaminaTag;                              // Attribute.Stamina
    Spend.Op            = EModifierType::Add;
    Spend.ValueType     = EGMCAttributeModifierType::AMT_Value;
    Spend.ModifierValue = -DashCost;
    AbilitySystem->ApplyAbilityAttributeModifier(Spend);
    CL_DoNotCombineNextMove();   // for the input flag, not the attribute (below)
}
```

The attribute itself needs no sealing: with `CombineIfUnchanged` a bound attribute that changes forces its own uncombined move, so the spend already lands on a move boundary. `CL_DoNotCombineNextMove()` (`Source/GMCCore/Public/Components/GMCReplicationComponent.h`) is for the input that triggered it: a flag still held next frame would otherwise let that frame combine into the move and re-run the tick from the pre-spend state (`gmas:gmc-prediction`).

Limits of a direct call: only `AMT_Value` resolves (the other sources read through the source effect, which is null: an Error and `0`; on 1.4.0 `AMT_Custom` hit `checkNoEntry`); `Conditions` are not evaluated; never set `bRegisterInHistory` without a live effect (an entry without an instigator is an `Orphan ... Modifier` Error and is skipped on 1.4.1+, a `checkNoEntry` in `CalculateValue` on 1.4.0); on a client it does nothing for an unbound attribute. Prefer an `Instant` effect applied `Predicted` once the change deserves a name in the debugger.

**`SetAttributeValueByTag(Tag, NewValue, bResetModifiers)` is deprecated and inert** (removed in 1.5). It changes nothing on 1.3 and 1.4; 1.4.0 still returned `true`, 1.4.1+ returns `false` and logs one Warning per component. Do not use it, not even for bootstrap: starting values come from the row or `SetAttributeInitialValue`, changes from a `Set` / `SetReplace` modifier through an effect or `ApplyAbilityAttributeModifier`.

**Change notifications are presentation.** `OnAttributeChanged(Tag, OldValue, NewValue)` (dynamic) and `AddAttributeChangeDelegate` (native, same arguments) fire from `GenAncillaryTick` on the owner and the server and from `GenSimulationTick` on simulated proxies, for bound and unbound attributes alike, once per attribute whose `Value` differs from the last check. They never fire inside the prediction tick or during a replay: the ancillary tick runs after the frame's move, so `NewValue` is the frame's final value and `OldValue` the value at the previous ancillary tick; a correction surfaces as the net change since then (a rejected spend arrives as a refund) and several changes inside one frame collapse into one. Drive HUD, sounds and cosmetics from them; decide gameplay from `GetAttributeValueByTag` inside the move. `OnPreAttributeChanged` is never broadcast (since the attribute refactor) and is deprecated in 1.4.1 (removed in 1.5 with its payload `UGMCAttributeModifierContainer`): it is not restored because it would run Blueprint code that edits modifiers inside every predicted apply and every replay. Pre-change logic goes in a custom calculator, post-change logic in `OnAttributeChanged`.

**Inspect.** The Gameplay Debugger category `GMCAbilitySystem` lists every attribute of the debug actor, server and client side by side (`gmas:gmas-setup`). `GetAttributeByTag(Tag)->DumpDebugString()` (1.4+) prints `RawValue`, the clamp and every temporal entry with its kind and instigator. Console: `GMAS.LogApplyTrace` with `GMAS.ApplyTraceFilter` logs a call stack per effect application, and `GMAS.DumpAttrBindMap` (1.4.1+; `BL.GMAS.DumpAttrBindMap` on 1.4.0, kept as a deprecated alias for one minor) maps the local player's bound float indices to attribute tags (`gmas:gmas-debug`).

## Modifier math

`FGMCAttributeModifier` (`Public/Attributes/GMCAttributeModifier.h`): `AttributeTag` (the target), `Op`, `ValueType` with its payload (`ModifierValue`, `ValueAsAttribute`, `CustomModifierClass`, `ExternalTag` + `ExternalValueIndex`), op-specific fields (`X`, `Y`, `XAsAttribute`, `YAsAttribute`, `XAttribute`, `YAttribute`, `Attributes`) and `Conditions`. `V` below is the resolved source value; percentages are entered as percents (`25` = 25 %); ticking effects multiply the result by the tick's delta (`CalculateModifierValue`, `Private/Attributes/GMCAttributeModifier.cpp`).

| `EModifierType` | Contribution |
|---|---|
| `Add` | `V` |
| `AddPercentageInitialValue` | `V %` of `InitialValue` |
| `AddPercentageAttribute` | `V %` of `ValueAsAttribute`'s `Value` |
| `AddPercentageMaxClamp`, `AddPercentageMinClamp` | `V %` of the resolved upper / lower clamp. 1.4.1+: `0` (one Warning per attribute) while that bound's flag is off; 1.4.0 read the literal anyway |
| `AddPercentageAttributeSum` | `V %` of the summed `Value`s of the `Attributes` container |
| `AddScaledBetween` | `Lerp(X, Y, V)`, `V` a 0–1 alpha (not a percent); a bound is `XAttribute` / `YAttribute` when `XAsAttribute` / `YAsAttribute`. 1.4.1+: the alpha is clamped, so the result stays between the resolved bounds; 1.4.0 clamped to the literal `X`..`Y` even when the bounds were attributes |
| `AddClampedBetween` | `V` clamped to `X`..`Y` |
| `AddPercentageMissing` | `V %` of `InitialValue − Value` (not of the max clamp) |
| `AddPercentageOfAttributeRawValue` | `V %` of `ValueAsAttribute`'s `RawValue` when set, else the target's own `RawValue` (1.4.1+; 1.4.0 ignored `ValueAsAttribute`) |
| `Set` (1.4+) | the base layer becomes `V`; active `Add`s keep stacking on it; no delta scaling |
| `SetReplace` (1.4+) | the base layer becomes `V`; `Add`s placed before it are dropped; no delta scaling |
| `AddPercentageOfBase` (1.4+) | `V %` of the resolved base layer, computed at recalculation time |

`EGMCAttributeModifierType`, where `V` comes from:

| `ValueType` | Source |
|---|---|
| `AMT_Value` | `ModifierValue` |
| `AMT_Attribute` | `ValueAsAttribute` on the ASC the effect is applied to (the effect's owner, not the instigator). The same field names the source of `AddPercentageAttribute` and `AddPercentageOfAttributeRawValue` |
| `AMT_Custom` | `CustomModifierClass->Calculate(SourceEffect, TargetAttribute)`, run on the class default object |
| `AMT_External` (1.4+) | `GetExternalModifierValue(ExternalTag, ExternalValueIndex)`, a virtual on the ASC returning `0` until you override it; the result must be identical on the server and the owning client and stable within a move |

`Conditions` (1.4+, `FGMCModifierCondition`): rules evaluated in order against the owner's **bound** active tags; the first whose `FGameplayTagQuery` matches wins: `Skip` drops this application, `OverrideValue` swaps the value source (its own `ValueType` and payload). An empty query never matches. Effects evaluate them at every application; `bReevaluateConditionsWhilePersistent` on the effect data re-checks them while a persistent effect lives.

**Layering** (1.4+, `FAttribute::CalculateValue`, `Private/Attributes/GMCAttributes.cpp`). Permanent modifiers write `RawValue` as they arrive: `Set`/`SetReplace` overwrite it, `AddPercentageOfBase` scales it, the rest add. Temporal entries are layered on every recalculation: the base is the newest `Set`/`SetReplace` entry (ties go to the higher application index), else `RawValue`; each `PercentOfBase` entry adds its fraction of that frozen base; each `Add` entry follows in application order; in `SetReplace` mode entries older than the winning set are skipped. The clamp runs after every step, so the order in which `Add`s meet the cap matters. Trees older than 1.4 know only additive stacking.

## Patterns

- **Cap as an attribute.** `Attribute.MaxStamina` bound; `Attribute.Stamina` with `MaxAttributeTag` and `bStartFull`. The clamp reads the cap's current `Value`, so a temporal buff on the cap raises the ceiling at once, and lowering the cap clamps the value on the next recalculation. The cap must be bound when the value is: the clamp is evaluated inside the move.
- **Regeneration** = a `Ticking` effect with `Add +20` on `Attribute.Stamina` (per second, scaled by the tick's delta) or a `Periodic` effect applying the whole amount every `PeriodicInterval`; `Predicted` for a bound attribute, listed in `StartingEffects` for a permanent one. Leave `bNegateEffectAtEnd` off: the regenerated amount must stay when the effect ends. Continuous regeneration is the one case for `AlwaysCombineOverwrite`.
- **Server-owned stat** (health under server-side damage): an unbound row. The hit handler on the server applies an `Instant` effect with the `ServerInstantAttribute` queue type (1.4+; `ServerAuth` before); clients receive `Value` and `RawValue` through `UnBoundAttributes`, and `OnAttributeChanged` fires on their next ancillary or simulation tick. Nothing predicted may read it (`gmas:gmas-rules`).
- **Derived stat.** `Attribute.DashSpeed` with a `Persistent` effect, `bNegateEffectAtEnd` on, whose modifier is `AddPercentageAttribute` (or `AMT_Attribute`) of `Attribute.Agility` on the same ASC. The source is read when the modifier is applied, not continuously. To follow the source, tick `bReevaluateConditionsWhilePersistent` (1.4+, `FGMCAbilityEffectData`; needs `bNegateEffectAtEnd`): every tick the effect drops and re-applies each modifier's single temporal entry, re-reading the source without accumulating. Before 1.4, re-apply the effect when the source changes.
- **Identity-driven values.** Override `GetExternalModifierValue` (1.4+) to return loadout numbers from state every machine already holds (`gmas:gmas-rules`), and use `AMT_External` modifiers in the effects.
- **Custom calculator.** Subclass `UGMCAttributeModifierCustom_Base` (`Public/Attributes/GMCAttributeModifierCustom_Base.h`), override `float Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute)` without calling `Super` (the base forwards to the Blueprint event `K2_Calculate(SourceEffect, Tag)`). It runs on the class default object, so configuration lives in class defaults and the function must be pure: read the target's `Value`, `RawValue`, `InitialValue`, `Clamp`, or other attributes through `SourceEffect->GetOwnerAbilityComponent()`, and return the raw number the chosen `Op` then applies. Reference the class from a modifier with `ValueType = AMT_Custom`. The bundled `UGMCModifierCustom_Exponent` (`Public/Attributes/PreDefinedCustomCalculator/GMCModifierCustom_Exponent.h` on 1.4.1+, so C++ can reference or subclass it; private and editor-selectable only on 1.4.0) maps the target's `Value` through an exponential, easing, power or saturation curve into `Min`..`Max`.

```cpp
UCLASS()
class UMyModifier_Falloff : public UGMCAttributeModifierCustom_Base
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly) float FullAt = 50.f;
    virtual float Calculate(UGMCAbilityEffect* SourceEffect, const FAttribute* Attribute) override
    {
        if (!SourceEffect || !Attribute) { return 0.f; }
        return FMath::Clamp(Attribute->Value / FullAt, 0.f, 1.f);   // 0..1 from the target's current Value
    }
};
```

## Checklist

- Every row has a deliberate clamp: `Max` or `MaxAttributeTag`, or `bClampMax` off; same for the floor. No row relies on the pre-1.4 "all zero means unset" behaviour.
- `bGMCBound` is `true` only for attributes predicted logic reads and for the `MaxAttributeTag` / `MinAttributeTag` of a bound attribute; display-only values are unbound.
- `ValueCombineMode` stays `CombineIfUnchanged` unless the attribute changes every tick and feeds no movement math.
- `AttributeDataAssets` and any `SetAttributeInitialValue` override are identical on every machine and in place before the bind; each tag appears once.
- Changes go through effects or `ApplyAbilityAttributeModifier` inside the prediction tick on both sides, before the ASC's tick; unbound attributes change on the server only; nothing calls `SetAttributeValueByTag`.
- Direct calls use `AMT_Value` and never set `bRegisterInHistory`; a spend triggered by a held input flag is followed by `CL_DoNotCombineNextMove()` (the attribute's own change already splits the move).
- `OnAttributeChanged` / `AddAttributeChangeDelegate` drive presentation only; nothing binds the deprecated `OnPreAttributeChanged` or the struct's own delegate (removed in 1.4.1).
- `GetExternalModifierValue` returns the same value on the server and the owning client; custom calculators are pure and configured through class defaults.
- Verified under networked PIE with a client: no `LogGMCReplication` corrections when the attribute changes (`gmas:gmas-testing`).

## Which test to write

A `GMAS.*`-style spec that drives the attribute through a component, as `Source/GMCAbilitySystemTests/Private/GMAS_EffectSpec.cpp` and `GMAS_AttributeAuditSpec.cpp` (`GMAS.Unit.AttributeAudit`: init order, duplicate rows, the clamp ops) do (`Source/GMCAbilitySystem/Private/Tests/` before 1.4.1), on the stub harness `gmas:gmas-testing` describes, with a `UGMCAttributesData` carrying your real rows (same clamp, same `bGMCBound`). Apply the modifier (through the effect class or `ApplyAbilityAttributeModifier`), call `GenPredictionTick(DeltaTime)` so `ProcessAttributes` recalculates, and assert `GetAttributeValueByTag` and `GetAttributeRawValue`. One case per rule: the clamp holds at the cap, `bStartFull` starts at the cap, the custom calculator returns its curve, the external value reaches the attribute.

A bare `FAttribute` gets the default `[0, 0]` clamp from `Init()` and reads `0`: set `Clamp.bClampMin = false` and `Clamp.bClampMax = false`, or a real `Max`, before `Init()`, as the helpers `GMASTest::MakeAttr` / `MakeClampedAttr` (`GMAS_TestHelpers.h`, 1.4.1+) do. On 1.4.0 the bundled helpers did not, which is why about 150 `GMAS.Unit.Attribute*` specs failed there; on 1.4.1 they pass. Replay symmetry needs networked PIE (`gmas:gmas-testing`).
