---
name: gmas-setup
description: Use when adding GMAS to a project or wiring a new GMC pawn or movement component to a GMC_AbilitySystemComponent, in C++ or Blueprint, including submodule install and Build.cs dependencies.
---

> Behavior below was verified against GMC 2.3.x and GMAS 1.4. Confirm signatures in this project's own headers (paths are relative to each plugin's root, e.g. `Source/GMCCore/Public/Components/GMCReplicationComponent.h`); GMC's source is not part of this plugin.

This skill gets GMAS (GMC Ability System) into a project and a first pawn wired to it, in C++ or Blueprint. The rules that govern what you build afterwards live in `gmas:gmas-rules` (where state lives, binding order, effects, server operations) and `gmas:gmc-prediction` (GMC's move cycle). GMAS paths below are relative to `Source/GMCAbilitySystem/`; GMC paths to the GMC plugin root.

## Requirements

| Need | Detail |
|---|---|
| a GMC license | GMC (General Movement Component, GMCv2) is a paid plugin. Its source lives only in the project, under `Plugins/GMC` (module `GMCCore`); GMAS never ships it, and neither do the skills in this plugin. GMAS 1.4 is verified against GMC 2.3.x |
| Unreal 5.5 to 5.8 | 5.4 unverified: `Public/Utility/GMASBoundQueueV2.h` includes `StructUtils/InstancedStruct.h` without the `ENGINE_MINOR_VERSION >= 5` guard the other GMAS headers carry, and on 5.4 that header lives in the `StructUtils` plugin under a different path |
| plugins enabled in `.uproject` | `GMC` and `GMCAbilitySystem`. GMAS's `.uplugin` declares `GMC`, `StructUtils` and `Niagara` (1.4+) as plugin dependencies, so enabling GMAS enables them: Niagara is on by default anyway; `StructUtils` is a real plugin on 5.4 and a deprecated shim from 5.5 that still exists in 5.8. GMC's own `.uplugin` pulls `EnhancedInput` and `OnlineSubsystemSteam` |
| GameplayTags | an engine module, not a plugin: your `Build.cs` lists it; the tag editor plugin `GameplayTagsEditor` is enabled by default |
| GMC controllers | player controllers derive from `AGMC_PlayerController`, AI controllers from `AGMC_AIController`, and the game mode spawns exactly one `AGMC_WorldTimeReplicator` (`gmas:gmc-prediction`) |

GMAS has two modules: `GMCAbilitySystem` (runtime) and `GMCAbilitySystemEditor` (editor); both load at the `Default` phase.

## Installing GMAS

**Submodule** (recommended: upgrades are a pointer bump, and you can read the exact tree you build against):

```sh
git submodule add -b main https://github.com/reznok/GMCAbilitySystem.git Plugins/GMCAbilitySystem
git submodule update --init --recursive
```

- `-b main` follows releases (tags `v1.x.y`); `-b dev` follows the integration branch, which the repository's `docs/BRANCHING.md` recipe uses and which moves daily. To pin a release: `git -C Plugins/GMCAbilitySystem checkout v1.4.0`, then commit the pointer. To update: `git submodule update --remote Plugins/GMCAbilitySystem`, rebuild, commit the pointer (`gmas:gmas-upgrade`).
- Never patch inside the submodule: fix upstream, then bump the pointer. The plugin's name is `GMCAbilitySystem` whatever the folder is called.

**Copied folder:** download a release archive (or `git clone --depth 1 -b v1.4.0`) into `Plugins/GMCAbilitySystem` and drop its `.git`. You own updates from then on: diff against the release you copied when upgrading.

**`.uproject`:**

```json
"Plugins": [
    { "Name": "GMC", "Enabled": true },
    { "Name": "GMCAbilitySystem", "Enabled": true }
]
```

**`Build.cs` of every module that includes GMC or GMAS headers:**

```csharp
PublicDependencyModuleNames.AddRange(new string[] {
    "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "GameplayTags",
    "GMCCore", "GMCAbilitySystem"
});
```

`GMCAbilitySystem` exports `GMCCore`, `GameplayTags`, `GameplayTasks`, `EnhancedInput`, `StructUtils` and `NetCore` as public dependencies (`Source/GMCAbilitySystem/GMCAbilitySystem.Build.cs`), so its headers compile in your module without more. List `GMCCore` and `GameplayTags` anyway: your own headers use them directly. List `GameplayTasks` yourself when you subclass the ability system component (a `UGameplayTasksComponent`) or write ability tasks. `StructUtils` is normally not needed on any version: on 5.5+ `FInstancedStruct` lives in `CoreUObject` (header `StructUtils/InstancedStruct.h`), and on 5.4 UBT propagates the module through GMAS's public dependencies; add it only if your 5.4 build complains.

Regenerate project files, build (a source plugin compiles with the project), open the editor: *Edit → Plugins* lists "GMAS - GMC Ability System" under the GMAS category.

## C++ wiring

| Piece | Class | Notes |
|---|---|---|
| pawn | `AGMC_Pawn` (`Source/GMCCore/Public/Actors/GMCPawn.h`) | an `APawn` that owns GMC's move pipeline: finds its replication component (`GetReplicationComponent()`), binds it from `PostInitializeComponents` (`SetupReplicationComponent()` calls `BindReplicationData`), and handles possession and move-state replication |
| convenience pawn | `AGMAS_Pawn` (`Public/Actors/GMAS_Pawn.h`) | `AGMC_Pawn` plus a capsule root, skeletal mesh, spring arm, camera, an ability system component (`AbilitySystemComponent`, subobject `Ability Component`) and an optional `InputMappingContext` added at priority 0. No movement component: you add yours. Purely optional |
| movement component | `UGMC_OrganicMovementCmp` (`Source/GMCCore/Public/Components/GMCOrganicMovementComponent.h`) for characters; `UGMC_MovementUtilityCmp` (`GMCMovementUtilityComponent.h`, same folder) for anything else | your subclass overrides the five hooks below and forwards them |
| ability system component ("the ASC") | `UGMC_AbilitySystemComponent` (`Public/Components/GMCAbilityComponent.h`), a `UGameplayTasksComponent`, replicated by default | one per pawn. Its `GMCMovementComponent` pointer (`UGMC_MovementUtilityCmp*`, `BlueprintReadWrite`) is **set by you**: nothing in GMAS assigns it, and `BindReplicationData()` dereferences it at once |

**Timing.** `AGMC_Pawn::PostInitializeComponents` runs the bind, so your `BindReplicationData_Implementation` executes before any `BeginPlay`. The ASC reads `AttributeDataAssets` *during* the bind (`InstantiateAttributes`) and `StartingAbilities`, `AbilityMaps`, `StartingTags` in its own `BeginPlay`; `StartingEffects` go out from the server once the pawn has a controller. Fill all of them in class defaults, the constructor, or in the pawn's `PostInitializeComponents` before `Super`, identically on every machine (`gmas:gmas-rules`).

| Movement component override | Forward to the ASC |
|---|---|
| `BindReplicationData_Implementation()` | `Super`, your own `Bind*` calls, then `GMCMovementComponent = this` and `BindReplicationData()` as the **last** line |
| `PreLocalMoveExecution_Implementation(const FGMC_Move& LocalMove)` | `Super`, then `PreLocalMoveExecution()` |
| `GenPredictionTick_Implementation(float DeltaTime)` | `Super`, your predicted logic, then `GenPredictionTick(DeltaTime)` |
| `GenSimulationTick_Implementation(float DeltaTime)` | `Super`, then `GenSimulationTick(DeltaTime)` |
| `GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove)` | `Super`, then `GenAncillaryTick(DeltaTime, bCombinedClientMove)`; `bLocalMove` is not forwarded |

`UGMC_OrganicMovementCmp` implements four of the five itself (all but the ancillary tick), so always call `Super` first. Where the ASC call sits relative to your own logic inside a hook is yours to decide; keep it the same on every machine.

```cpp
// MyMovementCmp.h
#pragma once
#include "Components/GMCOrganicMovementComponent.h"
#include "Components/GMCAbilityComponent.h"
#include "MyMovementCmp.generated.h"

UCLASS()
class UMyMovementCmp : public UGMC_OrganicMovementCmp
{
    GENERATED_BODY()
public:
    virtual void BindReplicationData_Implementation() override;
    virtual void PreLocalMoveExecution_Implementation(const FGMC_Move& LocalMove) override;
    virtual void GenPredictionTick_Implementation(float DeltaTime) override;
    virtual void GenSimulationTick_Implementation(float DeltaTime) override;
    virtual void GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove) override;
protected:
    UPROPERTY(Transient) TObjectPtr<UGMC_AbilitySystemComponent> AbilitySystem;
};

// MyMovementCmp.cpp
void UMyMovementCmp::BindReplicationData_Implementation()
{
    Super::BindReplicationData_Implementation();
    // Your own Bind* calls go here (gmas:gmc-prediction).
    AbilitySystem = GetOwner()->FindComponentByClass<UGMC_AbilitySystemComponent>();
    check(AbilitySystem);
    AbilitySystem->GMCMovementComponent = this;   // nothing else sets this pointer
    AbilitySystem->BindReplicationData();          // the ASC binds last
}
void UMyMovementCmp::PreLocalMoveExecution_Implementation(const FGMC_Move& LocalMove)
{
    Super::PreLocalMoveExecution_Implementation(LocalMove);
    AbilitySystem->PreLocalMoveExecution();
}
void UMyMovementCmp::GenPredictionTick_Implementation(float DeltaTime)
{
    Super::GenPredictionTick_Implementation(DeltaTime);
    // Predicted game logic that abilities depend on goes here (bound tags via MatchTagToBool).
    AbilitySystem->GenPredictionTick(DeltaTime);
}
void UMyMovementCmp::GenSimulationTick_Implementation(float DeltaTime)
{
    Super::GenSimulationTick_Implementation(DeltaTime);
    AbilitySystem->GenSimulationTick(DeltaTime);
}
void UMyMovementCmp::GenAncillaryTick_Implementation(float DeltaTime, bool bLocalMove, bool bCombinedClientMove)
{
    Super::GenAncillaryTick_Implementation(DeltaTime, bLocalMove, bCombinedClientMove);
    AbilitySystem->GenAncillaryTick(DeltaTime, bCombinedClientMove);
}
```

The pawn adds the movement component (and, from a bare `AGMC_Pawn`, a root collision component plus `CreateDefaultSubobject<UGMC_AbilitySystemComponent>`); the pawn's root becomes GMC's updated component. Attribute data chosen at runtime (an archetype) is injected before the bind:

```cpp
// MyPawn.h  (includes: Actors/GMAS_Pawn.h, MyMovementCmp.h, Attributes/GMCAttributesData.h, InputAction.h;
//            the .cpp adds EnhancedInputComponent.h)
UCLASS()
class AMyPawn : public AGMAS_Pawn
{
    GENERATED_BODY()
public:
    AMyPawn(const FObjectInitializer& Init);
    virtual void PostInitializeComponents() override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
protected:
    void OnDash(const FInputActionInstance& Instance);
    UPROPERTY(VisibleAnywhere)  TObjectPtr<UMyMovementCmp> Movement;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UGMCAttributesData> Attributes;
    UPROPERTY(EditDefaultsOnly) TObjectPtr<UInputAction> DashAction;
};

// MyPawn.cpp
AMyPawn::AMyPawn(const FObjectInitializer& Init) : Super(Init)
{
    Movement = CreateDefaultSubobject<UMyMovementCmp>(TEXT("Movement"));
}
void AMyPawn::PostInitializeComponents()
{
    if (Attributes) { AbilitySystemComponent->AttributeDataAssets.AddUnique(Attributes); } // same asset everywhere
    Super::PostInitializeComponents();                                                       // GMC binds in here
}
```

## Blueprint wiring

The same five events, in the movement component Blueprint (a child of `GMC_OrganicMovementCmp`), with the ASC on the character. Step by step with screenshots: https://github.com/reznok/GMCAbilitySystem/wiki/Getting-Started (the wiki is stale: since 1.0 the tag-to-class rows live in `GMCAbilityMapData` assets, below, not in an editable map, and its `Ability.Jump` map key and activation tag are an `Input.*` tag today).

1. Character Blueprint: *Add Component → GMC Ability System Component* (or parent the Blueprint to `GMAS Pawn`, which already has one).
2. Movement component Blueprint: a variable of type *GMC Ability System Component*, filled from *Get Owner → Get Component by Class*.
3. *Event Bind Replication Data*: call the parent, do your own binds, set the ASC's `GMCMovementComponent` (advanced-display, writable) to *Self*, then call the ASC's *Bind Replication Data* last.
4. *Event Pre Local Move Execution*, *Event Gen Prediction Tick*, *Event Gen Simulation Tick*, *Event Gen Ancillary Tick*: parent call, then the ASC function of the same name with the event pins connected (`Delta Time`; the ancillary tick's `Combined Client Move`).
5. Input: on the input action's *Started* event call the ASC's *Activate Ability* node (the display name of `QueueAbility`) with the `Input.*` tag and the *Input Action* pin connected; ability tasks that wait for key release read it.

## Attributes, tags, abilities

**Tags.** *Project Settings → GameplayTags → Manage Gameplay Tags*, `Config/DefaultGameplayTags.ini`, or native tags (`UE_DEFINE_GAMEPLAY_TAG`). The editor pickers enforce the prefixes: `Attribute.*` (`FAttributeData::AttributeTag`), `Ability.*` (`UGMCAbility::AbilityTag`), `Input.*` (the tag `QueueAbility` takes; the ability map's key is not filtered but must be that same tag). Use `State.*` for your own active tags.

**Attributes** are rows of a `UGMCAttributesData` asset (`Public/Attributes/GMCAttributesData.h`, array `AttributeData` of `FAttributeData`), listed in the ASC's `AttributeDataAssets` (*Attributes* in Details):

| Field | Set it to |
|---|---|
| `AttributeTag` | `Attribute.Stamina` |
| `DefaultValue` | `100` (or `bStartFull` (1.4+) to start at the upper clamp) |
| `Clamp` (`FAttributeClamp`, `GMCAttributeClamp.h`) | `Max = 100` or `MaxAttributeTag = Attribute.MaxStamina`: **the default clamp holds the value at 0 (1.4+)** |
| `bGMCBound` | `true`, because predicted logic (the dash cost) reads it |

A `MaxAttributeTag` names another attribute of the same component (`Attribute.MaxStamina`, default `100`, bound like the value it clamps, since the clamp is evaluated inside the move). Field defaults, the clamp flags, `ValueCombineMode` and the bound-versus-unbound rule: `gmas:gmas-rules`; everything else about attributes: `gmas:gmas-attribute`.

**Ability map.** A `UGMCAbilityMapData` asset (`Public/Ability/GMCAbilityMapData.h`) holds rows of `FAbilityMapData`: `InputTag` (`Input.Dash`), `Abilities` (every class that may answer that tag; the first granted one decides which tick the batch runs on), `bGrantedByDefault` (`true`). List the asset in the ASC's `AbilityMaps`; at the ASC's `BeginPlay`, `InitializeAbilityMap` copies the rows into the runtime `AbilityMap` and grants every default row, i.e. adds its tag to the bound `GrantedAbilityTags`. Runtime changes: `AddAbilityMapData(UGMCAbilityMapData*)`, `RemoveAbilityMapData`, `GrantAbilityByTag(Tag)`, or an effect's `GrantedAbilities`.

**Starting lists on the ASC.** `StartingAbilities` (tag container, granted at `BeginPlay`: for rows with `bGrantedByDefault` off, or tags you grant without a row), `StartingEffects` (effect classes the server applies as `ServerAuth` effects once the pawn is possessed; the owning client receives them through the bound queue), `StartingTags` (appended to `ActiveTags` at `BeginPlay`).

## First ability

An ability is a `UGMCAbility` (`Public/Ability/GMCAbility.h`). Activation runs `PreBeginAbility` (cooldown, `PreExecuteCheckEvent`, blocked-by-other-ability checks) and then `BeginAbility`, which broadcasts `OnAbilityActivated`, commits the cooldown when `bApplyCooldownAtAbilityBegin` (default `true`, needs `AbilityTag`), cancels conflicting abilities and finally calls the `BeginAbilityEvent` native event. Override the events, not `BeginAbility`, so that bookkeeping stays. **Cost is not checked for you:** `CanAffordAbilityCost()` only answers; gate the activation with it yourself, and `CommitAbilityCost()` applies the `AbilityCost` effect class.

```cpp
// MyAbility_Dash.h
#include "Ability/GMCAbility.h"
#include "MyAbility_Dash.generated.h"
UCLASS()
class UMyAbility_Dash : public UGMCAbility
{
    GENERATED_BODY()
public:
    UMyAbility_Dash() { bActivateOnMovementTick = true; }   // the 1.4 default; false on older trees
    UPROPERTY(EditDefaultsOnly) float DashSpeed = 1200.f;
    virtual bool PreExecuteCheckEvent_Implementation() override { return CanAffordAbilityCost(); }
    virtual void BeginAbilityEvent_Implementation() override;
};
// MyAbility_Dash.cpp
void UMyAbility_Dash::BeginAbilityEvent_Implementation()
{
    CommitAbilityCost();                                             // AbilityCost: UMyEffect_DashCost, -20 Attribute.Stamina
    const FVector Forward = GetOwnerPawn()->GetActorForwardVector();
    GetOwnerMovementComponent()->AddImpulse(Forward * DashSpeed, /*bVelChange=*/true);
    EndAbility();
}
```

Class defaults (or a Blueprint child): `AbilityTag = Ability.Dash`, `AbilityCost = UMyEffect_DashCost` (an instant effect with one modifier, `Attribute.Stamina` `-20`; `gmas:gmas-effect`), `CooldownTime` if wanted. With `bActivateOnMovementTick = true` (1.4+ default; set it explicitly on older trees, where `false` runs the ability on the ancillary tick and the impulse lands outside the move) the impulse runs inside the move on the owning client and the server alike, so it is predicted and replayed without further work. Longer-lived abilities, tasks and the `bActivateOnMovementTick` choice: `gmas:gmas-ability`.

Wire it: one row `Input.Dash → UMyAbility_Dash` in the ability map asset (granted by default), then activate from input through `QueueAbility(FGameplayTag InputTag, const UInputAction* InputAction = nullptr, bool bPreventConcurrentActivation = false)`. On the owning client the activation rides the next move and is predicted; on a server-controlled pawn it becomes a server operation (`gmas:gmas-rules`):

```cpp
void AMyPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);          // AGMAS_Pawn adds InputMappingContext
    if (UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent))
    {
        Input->BindAction(DashAction, ETriggerEvent::Started, this, &AMyPawn::OnDash);
    }
}
void AMyPawn::OnDash(const FInputActionInstance& Instance)
{
    AbilitySystemComponent->QueueAbility(FGameplayTag::RequestGameplayTag(TEXT("Input.Dash")), Instance.GetSourceAction());
}
```

## Verify

- Play in editor and press `'` (the Gameplay Debugger's default `ActivationKey`, Apostrophe); enable the `GMCAbilitySystem` category (registered by the runtime module in slot 9). It shows, server and client side by side, the granted abilities, active abilities, bound and client-auth active tags, attributes, active effects and cached operations of the debug actor. Your attribute rows and the `Input.Dash` grant must be listed before you press anything.
- `Log LogGMCAbilitySystem Verbose` in the console. A press that does nothing logs `Ability Tag Not Granted` followed by `No Abilities Granted for InputTag` (the tag is not in `GrantedAbilityTags`: missing row, or `bGrantedByDefault` off and not in `StartingAbilities`), `Ability Tag Not Found ... Check The Component's AbilityMap` (granted but no row), or `Ability Activation for Ability.Dash Stopped ...` (cooldown, pre-execution check, blocking).
- Run it under networked PIE with at least one client, not only standalone: a wiring mistake (ASC bound before your values, a list filled on one machine only) shows up as corrections in `LogGMCReplication`, never standalone (`gmas:gmas-testing`).

## Checklist

- `GMC` and `GMCAbilitySystem` enabled in `.uproject`; GMAS is a submodule on `main` (or a pinned release) or a copied release; `Build.cs` lists `GMCCore`, `GMCAbilitySystem`, `GameplayTags` (and `EnhancedInput` for input).
- Pawn derives from `AGMC_Pawn` or `AGMAS_Pawn`; controllers from `AGMC_PlayerController` / `AGMC_AIController`; one `AGMC_WorldTimeReplicator` is spawned.
- The movement component overrides all five hooks, calls `Super` first in each, sets `GMCMovementComponent` and calls `BindReplicationData()` as the last line of its bind; `GenAncillaryTick` forwards `(DeltaTime, bCombinedClientMove)`.
- `AttributeDataAssets`, `AbilityMaps`, `StartingAbilities`, `StartingEffects`, `StartingTags` are filled before the bind or in class defaults, identically on every machine.
- Every attribute row has a deliberate clamp (`Max`, `MaxAttributeTag`, or `bClampMax` off) and `bGMCBound` only where predicted logic reads it.
- Tags use the prefixes the pickers enforce: `Attribute.*`, `Ability.*` (the ability's own tag), `Input.*` (map key and `QueueAbility` argument).
- The first ability runs on the movement tick (`bActivateOnMovementTick = true`, explicit on trees older than 1.4), gates on `CanAffordAbilityCost()` in `PreExecuteCheckEvent`, commits the cost, acts through `GetOwnerMovementComponent()`, and ends.
- Input calls `QueueAbility` with the input action; nothing calls `TryActivateAbility*` directly.
- The Gameplay Debugger category lists the attributes and the grant; the ability fires under networked PIE with a client.
