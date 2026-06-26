// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"

#include "GMCAbilitySystem.h"
#include "GMCOrganicMovementComponent.h"
#include "GMCPlayerController.h"
#include "NiagaraFunctionLibrary.h"
#include "Ability/GMCAbility.h"
#include "Ability/GMCAbilityMapData.h"
#include "Attributes/GMCAttributesData.h"
#include "Diagnostics/GMASReplayBurstSettings.h"
#include "Effects/GMCAbilityEffect.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformStackWalk.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "HAL/IConsoleManager.h"
#include "GameFramework/Pawn.h"

namespace GMASApplyTrace {
	// Diagnostic: dump full C++ + script callstack on every ApplyAbilityEffect that creates a
	// new local instance. Filters by class-name substring to keep the noise down (one effect at
	// a time). Set to empty to log all applies. Disabled when the substring is empty AND the
	// enable flag is off.
	static TAutoConsoleVariable<bool> CVarLogApplyTrace(
		TEXT("GMAS.LogApplyTrace"),
		false,
		TEXT("If true, dump C++ + script callstack on every ApplyAbilityEffect that creates a ")
		TEXT("local effect instance whose class name contains the substring in GMAS.ApplyTraceFilter."),
		ECVF_Default);

	static TAutoConsoleVariable<FString> CVarApplyTraceFilter(
		TEXT("GMAS.ApplyTraceFilter"),
		TEXT("Stamina_Recovery"),
		TEXT("Substring matched against effect class name. Only effects whose class contains this ")
		TEXT("substring are stack-traced when GMAS.LogApplyTrace is enabled. Empty = match all."),
		ECVF_Default);
}



// Sets default values for this component's properties
UGMC_AbilitySystemComponent::UGMC_AbilitySystemComponent(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	SetIsReplicatedByDefault(true);
}

FDelegateHandle UGMC_AbilitySystemComponent::AddFilteredTagChangeDelegate(const FGameplayTagContainer& Tags,
	const FGameplayTagFilteredMulticastDelegate::FDelegate& Delegate)
{
	TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>* MatchedPair = nullptr;

	for (auto& SearchPair : FilteredTagDelegates)
	{
		if (SearchPair.Key == Tags)
		{
			MatchedPair = &SearchPair;
		}
	}

	if (!MatchedPair)
	{
		MatchedPair = new(FilteredTagDelegates) TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>(Tags, FGameplayTagFilteredMulticastDelegate());
	}

	return MatchedPair->Value.Add(Delegate);
}

void UGMC_AbilitySystemComponent::RemoveFilteredTagChangeDelegate(const FGameplayTagContainer& Tags,
	FDelegateHandle Handle)
{
	for (int32 Index = FilteredTagDelegates.Num() - 1; Index >= 0; --Index)
	{
		TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>& SearchPair = FilteredTagDelegates[Index];
		if (SearchPair.Key == Tags)
		{
			SearchPair.Value.Remove(Handle);
			if (!SearchPair.Value.IsBound())
			{
				FilteredTagDelegates.RemoveAt(Index);
			}
			break;
		}
	}
}

FDelegateHandle UGMC_AbilitySystemComponent::AddAttributeChangeDelegate(
	const FGameplayAttributeChangedNative::FDelegate& Delegate)
{
	return NativeAttributeChangeDelegate.Add(Delegate);
}

void UGMC_AbilitySystemComponent::RemoveAttributeChangeDelegate(FDelegateHandle Handle)
{
	NativeAttributeChangeDelegate.Remove(Handle);
}

#if !UE_BUILD_SHIPPING
// Beautiful Light diagnostic: resolves a "[BLMoveEnqueue] ... Float[N]" index (logged by GMC's BL.GMC.LogMoveEnqueue)
// to the GMAS attribute it belongs to, for the local player's ability component. Float[BoundIndex] = <tag> Value;
// Float[BoundIndex+1] = <tag> RawValue. An index that matches no attribute is a GMC/movement bind (montage, ladder, ...).
static FAutoConsoleCommandWithWorld GBLDumpAttrBindMap(
	TEXT("BL.GMAS.DumpAttrBindMap"),
	TEXT("Logs the GMC bound-Float index -> GMAS attribute tag map for the local player (interprets [BLMoveEnqueue] Float[N])."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		const UGMC_AbilitySystemComponent* ASC = Pawn ? Pawn->FindComponentByClass<UGMC_AbilitySystemComponent>() : nullptr;
		if (!ASC)
		{
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("[BLMoveEnqueue] DumpAttrBindMap: no local player ability component found."));
			return;
		}
		UE_LOG(LogGMCAbilitySystem, Log, TEXT("[BLMoveEnqueue] --- Bound Float index -> attribute map (%s) ---"), *GetNameSafe(Pawn));
		for (const FAttribute& Attr : ASC->BoundAttributes.Attributes)
		{
			UE_LOG(LogGMCAbilitySystem, Log, TEXT("[BLMoveEnqueue] Float[%d]=%s (Value), Float[%d]=%s (RawValue) [combineMode=%d]"),
				Attr.BoundIndex, *Attr.Tag.ToString(), Attr.BoundIndex + 1, *Attr.Tag.ToString(), static_cast<int32>(Attr.ValueCombineMode));
		}
	})
);
#endif

void UGMC_AbilitySystemComponent::BindReplicationData()
{
	// Attribute Binds
	//
	InstantiateAttributes();

	// We sort our attributes alphabetically by tag so that it's deterministic.
	for (auto& AttributeForBind : BoundAttributes.Attributes)
	{
		// Combine mode is per-attribute (FAttributeData::ValueCombineMode, default CombineIfUnchanged). An attribute
		// that changes every prediction tick (e.g. Stamina drain/regen) and does NOT feed movement can opt into
		// AlwaysCombineOverwrite to stop defeating GMC move-combining. Applied to both Value and RawValue.
		AttributeForBind.BoundIndex = GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.Value,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			AttributeForBind.ValueCombineMode,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.RawValue,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		AttributeForBind.ValueCombineMode,
		EGMC_SimulationMode::Periodic_Output,
		EGMC_InterpolationFunction::TargetValue);
	}
	
	// Granted Abilities
	GMCMovementComponent->BindGameplayTagContainer(GrantedAbilityTags,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	// Active Tags
	GMCMovementComponent->BindGameplayTagContainer(ActiveTags,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::Periodic_Output,
		EGMC_InterpolationFunction::TargetValue);
	
	// TaskData Bind
	GMCMovementComponent->BindInstancedStruct(TaskData,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	GMCMovementComponent->BindBool(bJustTeleported,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::PeriodicAndOnChange_Output,
		EGMC_InterpolationFunction::TargetValue);

	BoundQueueV2.BindToGMC(GMCMovementComponent);

	// ServerValidated (not ClientValidated): output-side divergence does not trigger
	// client replay — required because GMC's InstancedStruct equality is order-sensitive
	// and would replay-storm under apply/remove churn on this list.
	if (!ActiveEffectIDsBound.IsValid())
	{
		ActiveEffectIDsBound.InitializeAs<FGMASActiveEffectIDsState>();
	}
	GMCMovementComponent->BindInstancedStruct(ActiveEffectIDsBound,
		EGMC_PredictionMode::ServerAuth_Output_ServerValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::Periodic_Output,
		EGMC_InterpolationFunction::TargetValue);
}

void UGMC_AbilitySystemComponent::BoundActiveEffectIDs_Add(int EffectID)
{
	if (FGMASActiveEffectIDsState* State = ActiveEffectIDsBound.GetMutablePtr<FGMASActiveEffectIDsState>())
	{
		State->IDs.AddUnique(EffectID);
	}
}

void UGMC_AbilitySystemComponent::BoundActiveEffectIDs_Remove(int EffectID)
{
	if (FGMASActiveEffectIDsState* State = ActiveEffectIDsBound.GetMutablePtr<FGMASActiveEffectIDsState>())
	{
		State->IDs.Remove(EffectID);
	}
}

bool UGMC_AbilitySystemComponent::BoundActiveEffectIDs_Contains(int EffectID) const
{
	if (const FGMASActiveEffectIDsState* State = ActiveEffectIDsBound.GetPtr<FGMASActiveEffectIDsState>())
	{
		return State->IDs.Contains(EffectID);
	}
	return false;
}
void UGMC_AbilitySystemComponent::GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UGMC_AbilitySystemComponent::GenAncillaryTick)
	
	// Caution if you override Ancillarytick, this value should wrap up the override.
	bInAncillaryTick = true;

	// Drain any PredictedQueued operations buffered since the last tick.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_DrainPredictedOps)
		DrainPendingPredictedOperations();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_OnAncillaryTick_Broadcast)
		OnAncillaryTick.Broadcast(DeltaTime);
	}

	{
	TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_ProcessOperation)
	if (HasAuthority())
	{
		// Server processes client output payloads — only for REMOTE client pawns.
		// The listen server's own locally-controlled pawn does not submit "client
		// data" to itself, so SV_GetLastClientData().OutputState.InstancedStructs
		// is empty on the first frame, causing an out-of-bounds crash when
		// GetBoundInstancedStruct tries to access index BI_OperationData.
		// ruff: To fix crash when running standalone I changed IsLocallyControlledListenServerPawn to IsLocallyControlledServerPawn
		if (GMCMovementComponent->IsPlayerControlledPawn() && !GMCMovementComponent->IsLocallyControlledServerPawn())
		{
			const FGMC_PawnState OutputState = GMCMovementComponent->SV_GetLastClientData().OutputState;
			// First-frame guard for REMOTE player pawns: before the client's first move has
			// populated SV_RemoteMoveExecutionAux.LastRawMove, OutputState is a default-constructed
			// FGMC_PawnState whose InstancedStruct sync array is empty (Num()==0). The ruff guard
			// above (!IsLocallyControlledServerPawn) only excludes the listen-server LOCAL pawn, so
			// remote pawns still reach here and reading BI_OperationData (index 1) asserts
			// out-of-bounds. No bound payload yet means there is no operation to process — skip.
			if (OutputState.InstancedStruct.Num() > BoundQueueV2.BI_OperationData)
			{
				const FInstancedStruct ClientPayloadOperationData = GMCMovementComponent->GetBoundInstancedStruct(BoundQueueV2.BI_OperationData, OutputState);
				ServerProcessOperation(ClientPayloadOperationData, false);
			}
		}
		// [EXPERIMENTAL] Removed second GenPreLocalMoveExecution() call here:
		// PreLocalMoveExecution already drained ClientQueuedOperations earlier in
		// this frame, leaving OperationData = AbilityActivationOp(opID) for ops
		// that PredictionTick rejected (e.g. abilities with bActivateOnMovementTick=false
		// running on listen server local pawn -- the gate at TryActivateAbilitiesByInputTag
		// line 525 returns false when bActivateOnMovementTick != bFromMovementTick=true).
		// The original re-drain stomped that payload back to an empty base struct
		// (queue is empty by now), silently dropping the ability. Symmetric with
		// the client else-branch below at line 223 which never re-drained.
		//
		// Safety analysis:
		// - AI / dedicated-server-with-remote-client pawns: GenPreLocalMoveExecution
		//   early-returns on non-locally-controlled server pawns (GMASBoundQueueV2.cpp:76-80),
		//   so the call was a no-op there.
		// - Listen server local pawn: empty queue → was setting OperationData to
		//   empty base, destroying the deferred ability payload (the bug).
		// - Server-side ClientQueuedOperations cannot be populated (all 4 callers
		//   gated by !HasAuthority(), and RPCOnServerOperationAdded is Client-RPC).
		//   CheckValidState (line 178 of GMASBoundQueueV2.cpp) already logs an
		//   Error if it ever happens -- the drain was idempotent defensive code.
		ProcessOperation(BoundQueueV2.OperationData, false);
	}
	else
	{
		ProcessOperation(BoundQueueV2.OperationData, false);
	}
	} // GMAS_Anc_ProcessOperation

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_CheckActiveTagsChanged)
		CheckActiveTagsChanged();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_ProcessAttributes)
		ProcessAttributes(false);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_CheckAttributeChanged)
		CheckAttributeChanged();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_CheckUnBoundAttributeChanged)
		CheckUnBoundAttributeChanged();
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_TickActiveCooldowns)
		TickActiveCooldowns(DeltaTime);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_BoundQueueV2_GenAncillaryTick)
		BoundQueueV2.GenAncillaryTick(DeltaTime);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_SendTaskDataToActiveAbility)
		SendTaskDataToActiveAbility(false);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_TickAncillaryActiveAbilities)
		TickAncillaryActiveAbilities(DeltaTime);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Anc_ClearAbilityAndTaskData)
		ClearAbilityAndTaskData();
	}

	// Replay-burst diagnostic — consume the sticky flag set by any GenPredictionTick
	// invocations during this frame's replay loop. AncillaryTick runs once per real
	// frame, so this collapses N replayed-move ticks into a single observation.
	if (bReplayObservedThisFrame)
	{
		bReplayObservedThisFrame = false;
		ProcessReplayBurstDiagnostic();
	}

	bInAncillaryTick = false;
}

UGMCAbilityEffect* UGMC_AbilitySystemComponent::GetActiveEffectByHandle(int EffectID) const
{
	return ActiveEffects.Contains(EffectID) ? ActiveEffects[EffectID] : nullptr;
}

TArray<UGMCAbilityEffect*> UGMC_AbilitySystemComponent::GetActiveEffectsByTag(const FGameplayTag& GameplayTag, bool bMatchExact) const
{
	TArray<UGMCAbilityEffect*> ActiveEffectsFound;

	for (const TTuple<int, UGMCAbilityEffect*>& EffectFound : ActiveEffects) {
		if (IsValid(EffectFound.Value) && bMatchExact ? EffectFound.Value->EffectData.EffectTag.MatchesTagExact(GameplayTag) : EffectFound.Value->EffectData.EffectTag.MatchesTag(GameplayTag)) {
			ActiveEffectsFound.Add(EffectFound.Value);
		}
	}

	return ActiveEffectsFound;
}


UGMCAbilityEffect* UGMC_AbilitySystemComponent::GetFirstActiveEffectByTag(const FGameplayTag& GameplayTag) const
{
	for (auto& EffectFound : ActiveEffects) {
		if (EffectFound.Value && EffectFound.Value->EffectData.EffectTag.MatchesTag(GameplayTag)) {
			return EffectFound.Value;
		}
	}

	return nullptr;
}


void UGMC_AbilitySystemComponent::AddAbilityMapData(UGMCAbilityMapData* AbilityMapData)
{
	for (const FAbilityMapData& Data : AbilityMapData->GetAbilityMapData())
	{
		AddAbilityMapData(Data);
	}
}

void UGMC_AbilitySystemComponent::RemoveAbilityMapData(UGMCAbilityMapData* AbilityMapData)
{
	for (const FAbilityMapData& Data : AbilityMapData->GetAbilityMapData())
	{
		RemoveAbilityMapData(Data);
	}
}

void UGMC_AbilitySystemComponent::AddStartingEffects(TArray<TSubclassOf<UGMCAbilityEffect>> EffectsToAdd)
{
	for (const TSubclassOf<UGMCAbilityEffect>& Effect : EffectsToAdd)
	{
		StartingEffects.AddUnique(Effect);
	}
}

void UGMC_AbilitySystemComponent::RemoveStartingEffects(TArray<TSubclassOf<UGMCAbilityEffect>> EffectsToRemove)
{
	for (const TSubclassOf<UGMCAbilityEffect>& Effect : EffectsToRemove)
	{
		StartingEffects.Remove(Effect);
	}
}

void UGMC_AbilitySystemComponent::GrantAbilityByTag(const FGameplayTag AbilityTag)
{
	if (!GrantedAbilityTags.HasTagExact(AbilityTag))
	{
		GrantedAbilityTags.AddTag(AbilityTag);
	}
}

void UGMC_AbilitySystemComponent::RemoveGrantedAbilityByTag(const FGameplayTag AbilityTag)
{
	if (GrantedAbilityTags.HasTagExact(AbilityTag))
	{
		GrantedAbilityTags.RemoveTag(AbilityTag);
	}
}

bool UGMC_AbilitySystemComponent::HasGrantedAbilityTag(const FGameplayTag GameplayTag) const
{
	return GrantedAbilityTags.HasTagExact(GameplayTag);
}

void UGMC_AbilitySystemComponent::AddActiveTag(const FGameplayTag AbilityTag)
{
	ActiveTags.AddTag(AbilityTag);
}

void UGMC_AbilitySystemComponent::RemoveActiveTag(const FGameplayTag AbilityTag)
{
	if (ActiveTags.HasTagExact(AbilityTag))
	{
		ActiveTags.RemoveTag(AbilityTag);
	}
}

void UGMC_AbilitySystemComponent::AddClientAuthActiveTag(const FGameplayTag Tag)
{
	ClientAuthActiveTags.AddTag(Tag);
}

void UGMC_AbilitySystemComponent::RemoveClientAuthActiveTag(const FGameplayTag Tag)
{
	if (ClientAuthActiveTags.HasTagExact(Tag))
	{
		ClientAuthActiveTags.RemoveTag(Tag);
	}
}

void UGMC_AbilitySystemComponent::AddSynchronizedTag(const FGameplayTag& Tag, bool AllowMultipleInstance)
{
	ensureAlwaysMsgf(HasAuthority(), TEXT("Only the server can add a synchronized tag"));

	if (!Tag.IsValid())
	{
		UE_LOGFMT(LogGMCAbilitySystem, Error, "{0} AbilityTag is invalid", __FUNCTION__);
		return;
	}

	if (!AllowMultipleInstance && HasActiveTag(Tag)) return;
	
	FGMCAbilityEffectData EffectData;
	EffectData.EffectTag = Tag;
	EffectData.GrantedTags = FGameplayTagContainer(Tag);
	EffectData.bServerAuth = true;
	EffectData.EffectType = EGMASEffectType::Persistent;

	bool OutSuccess;
	int OutEffectHandle;
	int OutEffectId;
	UGMCAbilityEffect* OutEffect;
	ApplyAbilityEffectSafe(UGMCAbilityEffect::StaticClass(), EffectData, EGMCAbilityEffectQueueType::ServerAuth, OutSuccess, OutEffectHandle,
	                       OutEffectId, OutEffect); 
}

void UGMC_AbilitySystemComponent::RemoveSynchronizedTag(const FGameplayTag& Tag, bool RemoveEveryInstance)
{
	ensureAlwaysMsgf(HasAuthority(), TEXT("Only the server can remove a synchronized tag"));

	if (!Tag.IsValid())
	{
		UE_LOGFMT(LogGMCAbilitySystem, Error, "{0} AbilityTag is invalid", __FUNCTION__);
		return;
	}
	
	RemoveActiveAbilityEffectByTag(Tag, EGMCAbilityEffectQueueType::ServerAuth, RemoveEveryInstance);
}

FGameplayTagContainer UGMC_AbilitySystemComponent::GetActiveTags() const
{
	// Concatenated union of bound (GMC-validated) and client-auth (locally maintained) tags.
	// O(N+M) per call. Not cached -- containers mutate frequently and the merge cost is small.
	FGameplayTagContainer Combined = ActiveTags;
	Combined.AppendTags(ClientAuthActiveTags);
	return Combined;
}

bool UGMC_AbilitySystemComponent::HasActiveTag(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTag(GameplayTag) || ClientAuthActiveTags.HasTag(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasActiveTagExact(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTagExact(GameplayTag) || ClientAuthActiveTags.HasTagExact(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasAnyTag(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAny(TagsToCheck) || ClientAuthActiveTags.HasAny(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAnyTagExact(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAnyExact(TagsToCheck) || ClientAuthActiveTags.HasAnyExact(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllTags(const FGameplayTagContainer TagsToCheck) const
{
	// HasAll is a conjunction over the union; cannot decompose into HasAll(A) || HasAll(B).
	// Materialise the union once and run the predicate on the merged container.
	FGameplayTagContainer Union = ActiveTags;
	Union.AppendTags(ClientAuthActiveTags);
	return Union.HasAll(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllTagsExact(const FGameplayTagContainer TagsToCheck) const
{
	FGameplayTagContainer Union = ActiveTags;
	Union.AppendTags(ClientAuthActiveTags);
	return Union.HasAllExact(TagsToCheck);
}

TArray<FGameplayTag> UGMC_AbilitySystemComponent::GetActiveTagsByParentTag(const FGameplayTag ParentTag){
	TArray<FGameplayTag> MatchedTags;
	if(!ParentTag.IsValid()) return MatchedTags;
	for(FGameplayTag Tag : ActiveTags){
		if(Tag.MatchesTag(ParentTag)){
			MatchedTags.Add(Tag);
		}
	}
	for(FGameplayTag Tag : ClientAuthActiveTags){
		if(Tag.MatchesTag(ParentTag)){
			MatchedTags.AddUnique(Tag);
		}
	}
	return MatchedTags;
}

// ---- Bound-only query variants -------------------------------------------------------

bool UGMC_AbilitySystemComponent::HasBoundActiveTag(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTag(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasBoundActiveTagExact(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTagExact(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasAnyBoundTag(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAny(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAnyBoundTagExact(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAnyExact(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllBoundTags(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAll(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllBoundTagsExact(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAllExact(TagsToCheck);
}

TArray<FGameplayTag> UGMC_AbilitySystemComponent::GetBoundActiveTagsByParentTag(const FGameplayTag ParentTag)
{
	TArray<FGameplayTag> MatchedTags;
	if (!ParentTag.IsValid()) return MatchedTags;
	for (FGameplayTag Tag : ActiveTags)
	{
		if (Tag.MatchesTag(ParentTag))
		{
			MatchedTags.Add(Tag);
		}
	}
	return MatchedTags;
}

bool UGMC_AbilitySystemComponent::TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag, const UInputAction* InputAction,
	const bool bFromMovementTick, const bool bForce, const int SourceOperationID)
{

	auto GrantedAbilities = GetGrantedAbilitiesByTag(InputTag);
	if (GrantedAbilities.Num() == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("No Abilities Granted for InputTag: %s"), *InputTag.ToString());
		return false;
	}
	bool bFirstAbilityActivatesDuringMovementTick = GrantedAbilities[0]->GetDefaultObject<UGMCAbility>()->bActivateOnMovementTick;
	
	if (!bForce && bFirstAbilityActivatesDuringMovementTick != bFromMovementTick)
	{
		// If the first ability doesn't match the bFromMovementTick state, we can't activate any abilities
		return false;
	}
	
	// If any abilities don't match the first ability's state, we can't activate them (no mixing movement and ancillary abilities)
	for (const TSubclassOf<UGMCAbility>& ActivatedAbility : GrantedAbilities)
	{
		const UGMCAbility* AbilityCDO = ActivatedAbility->GetDefaultObject<UGMCAbility>();
		if (AbilityCDO && AbilityCDO->bActivateOnMovementTick != bFirstAbilityActivatesDuringMovementTick)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to Activate Ability: %s from %s, but it doesn't match bFromMovementTick state of other abilities"), *GetNameSafe(ActivatedAbility), bFromMovementTick ? TEXT("Movement") : TEXT("Ancillary"));
			return false;
		}
	}
	
	// Operation-derived AbilityIDs: both sides iterate the same granted list (bound
	// replicated tags) in the same order, so (SourceOperationID, index) names the same
	// logical activation on client and server.
	for (int ActivationIndex = 0; ActivationIndex < GrantedAbilities.Num(); ActivationIndex++)
	{
		const int ForcedAbilityID = SourceOperationID != 0
			? DeriveAbilityIDFromOperation(SourceOperationID, ActivationIndex)
			: 0;
		TryActivateAbility(GrantedAbilities[ActivationIndex], InputAction, InputTag, false, ForcedAbilityID);
	}

	return true;
}

bool UGMC_AbilitySystemComponent::TryActivateAbility(const TSubclassOf<UGMCAbility> ActivatedAbility, const UInputAction* InputAction, const FGameplayTag ActivationTag, const bool bSkipActivationTagsCheck, const int ForcedAbilityID)
{

	if (ActivatedAbility == nullptr) return false;

	int AbilityID;
	if (ForcedAbilityID != 0)
	{
		// Operation-derived ID: shared with the remote side by construction (it travels in
		// the activation payload). If it's already live, this exact logical activation has
		// already run here — e.g. a client replay re-delivering the same activation op —
		// so re-running it would create a duplicate instance the other side doesn't have.
		AbilityID = ForcedAbilityID;
		if (ActiveAbilities.Contains(AbilityID))
		{
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s skipped: operation-derived ID %d already active (duplicate delivery/replay)."),
				*GetNameSafe(ActivatedAbility), AbilityID);
			return false;
		}
	}
	else
	{
		// Fallback for activations with no source operation (server-local: AI, debug).
		// Generated ID is based on ActionTimer so it tends to line up on client/server,
		// but it is NOT guaranteed to — paired activations must go through the
		// operation-derived path above.
		AbilityID = GenerateAbilityID();
	}

	const UGMCAbility* AbilityCDO = ActivatedAbility->GetDefaultObject<UGMCAbility>();
	if (!AbilityCDO->bAllowMultipleInstances)
	{
		// Enforce only one active instance of the ability at a time.
		if (GetActiveAbilityCount(ActivatedAbility) > 0) {
			UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("Ability Activation for %s Stopped (Already Instanced)"), *GetNameSafe(ActivatedAbility));
			return false;
		}
	}

	// Check Activation Tags
	if (!bSkipActivationTagsCheck && !CheckActivationTags(AbilityCDO)){
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Tags"), *GetNameSafe(ActivatedAbility));
		return false;
	}

	// Collision bump for the FALLBACK path only. Never for operation-derived IDs: bumping
	// would silently rename the activation on one side only, desyncing it from its twin
	// (the historical root cause of paired confirm-timeout + heartbeat-watchdog kills).
	if (ForcedAbilityID == 0)
	{
		while (ActiveAbilities.Contains(AbilityID)){
			AbilityID += 1;
		}
	}

	UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[Server: %hhd] Generated Ability Activation ID: %d"), HasAuthority(), AbilityID);
	
	UGMCAbility* Ability = NewObject<UGMCAbility>(this, ActivatedAbility);
	Ability->AbilityData = AbilityData;
	Ability->AbilityData.InputTag = ActivationTag;

	// Propagate class-identity and activation properties from the CDO to the instance.
	// NewObject does not reliably copy UPROPERTY values when called without a full
	// world context (e.g. headless automation, transient outers).  Explicit assignment
	// ensures the instance always matches its class defaults for every property that
	// is read on the instance rather than on the CDO.
	Ability->AbilityTag             = AbilityCDO->AbilityTag;
	Ability->CooldownTime           = AbilityCDO->CooldownTime;
	Ability->AbilityCost            = AbilityCDO->AbilityCost;
	Ability->BlockedByOtherAbility  = AbilityCDO->BlockedByOtherAbility;
	Ability->BlockOtherAbility      = AbilityCDO->BlockOtherAbility;
	Ability->CancelAbilitiesWithTag = AbilityCDO->CancelAbilitiesWithTag;
	Ability->AbilityDefinition      = AbilityCDO->AbilityDefinition;

	Ability->Execute(this, AbilityID, InputAction);
	ActiveAbilities.Add(AbilityID, Ability);

	// Only signal "confirmed" to the client if the server-side Execute did NOT bail out
	// during PreBeginAbility (cooldown, PreExecuteCheck, blocked-by-ability, blocked-by-tag).
	// CancelAbility sets AbilityState = Ended *before* BeginAbility runs; without this
	// gate the client receives RPCConfirmAbilityActivation for an ability the server
	// just cancelled, sets bServerConfirmed=true, and the Tick-time
	// `ClientStartTime + ServerConfirmTimeout < ActionTimer` check never fires —
	// the predicted ability runs indefinitely on the client.
	// Skip the confirm RPC when the activation came from the client-auth path
	// (the client never expects confirmation for trust-based activations).
	if (HasAuthority() && Ability->AbilityState != EAbilityState::Ended && !bSkipActivationTagsCheck)
	{
		RPCConfirmAbilityActivation(AbilityID);
	}

	return true;
}

void UGMC_AbilitySystemComponent::QueueAbility(FGameplayTag InputTag, const UInputAction* InputAction, bool bPreventConcurrentActivation)
{
	if (GetOwnerRole() != ROLE_AutonomousProxy && GetOwnerRole() != ROLE_Authority) return;

	// Detect client-auth path before standard routing.
	TArray<TSubclassOf<UGMCAbility>> Candidates = GetGrantedAbilitiesByTag(InputTag);
	for (const TSubclassOf<UGMCAbility>& AbilityClass : Candidates)
	{
		if (!AbilityClass) continue;
		if (!IsClientAuthorizedAbility(AbilityClass)) continue;

		// Hard contract: client-auth abilities MUST NOT run on the movement tick.
		const UGMCAbility* CDO = AbilityClass->GetDefaultObject<UGMCAbility>();
		if (!ensureMsgf(!CDO->bActivateOnMovementTick,
			TEXT("Client-auth ability %s has bActivateOnMovementTick=true -- replay incoherence guaranteed."),
			*AbilityClass->GetName()))
		{
			continue;  // skip this candidate, fall through to standard path
		}

		if (TryActivateClientAuthAbility(AbilityClass, InputTag, InputAction))
		{
			return;  // handled through client-auth path
		}
		// else fall through to standard path
	}

	// Existing standard flow continues unchanged below.
	FGMASBoundQueueV2AbilityActivationOperation ActivationData;
	ActivationData.InputTag = InputTag;
	ActivationData.InputAction = InputAction;
	const int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2AbilityActivationOperation>(ActivationData);

	if (!HasAuthority())
	{
		BoundQueueV2.QueueClientOperation(OperationID);
	}
	else
	{
		BoundQueueV2.QueueServerOperation(OperationID);
	}
	
}

int32 UGMC_AbilitySystemComponent::GetQueuedAbilityCount(FGameplayTag AbilityTag)
{
	return 0;
	// return QueuedAbilityOperations.NumMatching(AbilityTag, EGMASBoundQueueOperationType::Activate);
}

int32 UGMC_AbilitySystemComponent::GetActiveAbilityCount(TSubclassOf<UGMCAbility> AbilityClass)
{
	int32 Result = 0;

	for (const auto& ActiveAbilityData : ActiveAbilities)
	{
		if (ActiveAbilityData.Value->IsA(AbilityClass) && ActiveAbilityData.Value->AbilityState != EAbilityState::Ended) Result++;
	}

	return Result;
}


bool UGMC_AbilitySystemComponent::IsAbilityTagBlocked(const FGameplayTag AbilityTag) const {
	
	for (const auto& ActiveAbility : ActiveAbilities) {
		if (IsValid(ActiveAbility.Value) && ActiveAbility.Value->AbilityState != EAbilityState::Ended)
		{
			for (auto& Tag : ActiveAbility.Value->BlockOtherAbility) {
				if (AbilityTag.MatchesTag(Tag)) {
					UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability can't activate, blocked by Ability: %s"), *ActiveAbility.Value->GetName());
					return true;
				}
			}
		}
	}

	return false;
}


int UGMC_AbilitySystemComponent::EndAbilitiesByTag(FGameplayTag AbilityTag) {
	int AbilitiesEnded = 0;
	for (const auto& ActiveAbilityData : ActiveAbilities)
	{
		if (ActiveAbilityData.Value->AbilityTag.MatchesTag(AbilityTag))
		{
			ActiveAbilityData.Value->EndAbility();
			AbilitiesEnded++;
		}
	}
	return AbilitiesEnded;
}


int UGMC_AbilitySystemComponent::EndAbilitiesByClass(TSubclassOf<UGMCAbility> AbilityClass) {
	int AbilitiesEnded = 0;
	for (const auto& ActiveAbilityData : ActiveAbilities)
	{
		if (ActiveAbilityData.Value->IsA(AbilityClass))
		{
			ActiveAbilityData.Value->EndAbility();
			AbilitiesEnded++;
		}
	}
	return AbilitiesEnded;
}


int UGMC_AbilitySystemComponent::EndAbilitiesByQuery(const FGameplayTagQuery& Query)
{
	int AbilitiesEnded = 0;

	for (const auto& ActiveAbilityData : ActiveAbilities)
	{
		if (UGMCAbility* Ability = ActiveAbilityData.Value)
		{
			if (Query.Matches(Ability->AbilityDefinition))
			{
				Ability->SetPendingEnd();
				AbilitiesEnded++;
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("cancelled ability %s by query"), *Ability->AbilityTag.ToString());
			}
		}
	}
	return AbilitiesEnded;
}


int32 UGMC_AbilitySystemComponent::GetActiveAbilityCountByTag(FGameplayTag AbilityTag)
{
	int32 Result = 0;

	for (const auto& Ability : GetGrantedAbilitiesByTag(AbilityTag))
	{
		Result += GetActiveAbilityCount(Ability);
	}

	return Result;
}

void UGMC_AbilitySystemComponent::QueueTaskData(const FInstancedStruct& InTaskData)
{
	QueuedTaskData.Push(InTaskData);
}

void UGMC_AbilitySystemComponent::SetCooldownForAbility(const FGameplayTag AbilityTag, float CooldownTime)
{
	if (AbilityTag == FGameplayTag::EmptyTag) return;

	// Store absolute expiry in ActionTimer units. See ActiveCooldowns
	// declaration for why expiry-time (vs remaining-duration) is required.
	const double ExpiryActionTime = ActionTimer + static_cast<double>(CooldownTime);
	ActiveCooldowns.FindOrAdd(AbilityTag) = ExpiryActionTime;
}

float UGMC_AbilitySystemComponent::GetCooldownForAbility(const FGameplayTag AbilityTag) const
{
	if (const double* Expiry = ActiveCooldowns.Find(AbilityTag))
	{
		const double Remaining = *Expiry - ActionTimer;
		return Remaining > 0.0 ? static_cast<float>(Remaining) : 0.f;
	}
	return 0.f;
}


float UGMC_AbilitySystemComponent::GetMaxCooldownForAbility(TSubclassOf<UGMCAbility> Ability) const {
	return Ability ? Ability.GetDefaultObject()->CooldownTime : 0.f;
}


TMap<FGameplayTag, float> UGMC_AbilitySystemComponent::GetCooldownsForInputTag(const FGameplayTag InputTag)
{
	TArray<TSubclassOf<UGMCAbility>> Abilities = GetGrantedAbilitiesByTag(InputTag);

	TMap<FGameplayTag, float> Cooldowns;

	for (auto Ability : Abilities)
	{
		FGameplayTag AbilityTag = Ability.GetDefaultObject()->AbilityTag;
		Cooldowns.Add(AbilityTag, GetCooldownForAbility(AbilityTag));
	}

	return Cooldowns;
}

void UGMC_AbilitySystemComponent::MatchTagToBool(const FGameplayTag& InTag, bool MatchedBool){
	if(!InTag.IsValid()) return;
	if(MatchedBool){
		AddActiveTag(InTag);
	}
	else{
		RemoveActiveTag(InTag);
	}
}

bool UGMC_AbilitySystemComponent::IsServerOnly() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return !Pawn->IsPlayerControlled();
	}
	return true;
}

void UGMC_AbilitySystemComponent::DrawDebugAttribute(const FGameplayTag& AttributeTag) const
{
	const FAttribute* Attribute = GetAttributeByTag(AttributeTag);
	if (!Attribute) return;

	const FString Context = GMCMovementComponent->IsAutonomousProxy() ? TEXT("[AP]") : GMCMovementComponent->IsSimulatedPawn() ? TEXT("[SP]") : TEXT("[SRV]");
	UE_LOG(LogTemp, Warning, TEXT("%s Attribute: %s"), *Context, *Attribute->ToString());
}

void UGMC_AbilitySystemComponent::GenPredictionTick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UGMC_AbilitySystemComponent::GenPredictionTick)

	bJustTeleported = false;
	ActionTimer = GMCMovementComponent->GetMoveTimestamp();

	// Replay-burst diagnostic — sticky flag, consumed by GenAncillaryTick.
	// Set early so even short-circuiting paths below still record the observation.
	if (GMCMovementComponent && GMCMovementComponent->CL_IsReplaying())
	{
		bReplayObservedThisFrame = true;
	}

	// Drain any PredictedQueued operations buffered since the last tick.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_DrainPredictedOps)
		DrainPendingPredictedOperations();
	}

	{
	TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_ProcessOperation)
	// Same listen-server guard as GenAncillaryTick: skip client payload processing
	// for the locally-controlled host pawn (no client data submitted to itself).
	// ruff: To fix crash when running standalone I changed IsLocallyControlledListenServerPawn to IsLocallyControlledServerPawn
	if (HasAuthority() && GMCMovementComponent->IsPlayerControlledPawn() && !GMCMovementComponent->IsLocallyControlledServerPawn())
	{
		// Server processes client output payloads
		const FGMC_PawnState OutputState = GMCMovementComponent->SV_GetLastClientData().OutputState;
		const FInstancedStruct ClientPayloadOperationData = GMCMovementComponent->GetBoundInstancedStruct(BoundQueueV2.BI_OperationData, OutputState);
		ServerProcessOperation(ClientPayloadOperationData, true);
	}
	else
	{
		ProcessOperation(BoundQueueV2.OperationData, true);
	}
	} // GMAS_Pred_ProcessOperation

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_ApplyStartingEffects)
		ApplyStartingEffects();
	}

	// Purge "future" temporary modifiers on replay
	if (GMCMovementComponent->CL_IsReplaying())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_PurgeTemporalModifiers)
		for (FAttribute& Attribute : BoundAttributes.Attributes)
		{
			Attribute.PurgeTemporalModifier(ActionTimer);
		}
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_SendTaskDataToActiveAbility)
		SendTaskDataToActiveAbility(true);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_TickActiveAbilities)
		TickActiveAbilities(DeltaTime);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_TickActiveEffects)
		TickActiveEffects(DeltaTime);
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_ProcessAttributes)
		ProcessAttributes(true);
	}

	// Abilities
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GMAS_Pred_CleanupStaleAbilities)
		CleanupStaleAbilities();
	}
}

void UGMC_AbilitySystemComponent::GenSimulationTick(float DeltaTime)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UGMC_AbilitySystemComponent::GenSimulationTick)
	if (!GMCMovementComponent->IsSmoothedListenServerPawn())
	{
		CheckActiveTagsChanged();
		CheckAttributeChanged();
		CheckUnBoundAttributeChanged();
	}
	
	if (GMCMovementComponent->GetSmoothingTargetIdx() == -1) return;	
	const FVector TargetLocation = GMCMovementComponent->MoveHistory[GMCMovementComponent->GetSmoothingTargetIdx()].OutputState.ActorLocation.Read();
	if (bJustTeleported)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Teleporting %f Units"), FVector::Distance(GetOwner()->GetActorLocation(), TargetLocation));
		GetOwner()->SetActorLocation(TargetLocation);
		bJustTeleported = false;
	}
}

void UGMC_AbilitySystemComponent::DrainPendingPredictedOperations()
{
	if (PendingPredictedOperations.IsEmpty()) return;

	// MoveTemp to avoid re-entrancy issues if processing triggers another PredictedQueued call
	// (which would take the immediate path since we're now inside a tick).
	TArray<FInstancedStruct> Ops = MoveTemp(PendingPredictedOperations);
	PendingPredictedOperations.Reset();

	for (const FInstancedStruct& OpData : Ops)
	{
		if (!OpData.IsValid()) continue;

		if (OpData.GetScriptStruct() == FGMASBoundQueueV2ApplyEffectOperation::StaticStruct())
		{
			const FGMASBoundQueueV2ApplyEffectOperation& ApplyOp = OpData.Get<FGMASBoundQueueV2ApplyEffectOperation>();
			UGMCAbilityEffect* Effect = DuplicateObject(ApplyOp.EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			ApplyAbilityEffect(Effect, ApplyOp.EffectData);
		}
		else if (OpData.GetScriptStruct() == FGMASBoundQueueV2RemoveEffectOperation::StaticStruct())
		{
			const FGMASBoundQueueV2RemoveEffectOperation& RemoveOp = OpData.Get<FGMASBoundQueueV2RemoveEffectOperation>();
			for (const int Id : RemoveOp.EffectIDs)
			{
				if (ActiveEffects.Contains(Id))
				{
					RemoveActiveAbilityEffect(ActiveEffects[Id]);
				}
			}
		}
	}
}

void UGMC_AbilitySystemComponent::PreLocalMoveExecution()
{
	if (QueuedTaskData.Num() > 0)
	{
		// FIFO. Tasks chained inside a single BP graph push payloads in the
		// order their tasks were created. The receiving side needs them in
		// the same order so the dependent task's RunningTasks entry exists
		// by the time its payload arrives. LIFO (TArray::Pop) reverses the
		// order, causing the dependent task's payload to arrive before its
		// own RegisterTask call has run on the remote side → silent dispatch
		// failure in HandleTaskData (TaskID lookup miss).
		TaskData = QueuedTaskData[0];
		QueuedTaskData.RemoveAt(0);
	}
	BoundQueueV2.GenPreLocalMoveExecution();
}

void UGMC_AbilitySystemComponent::RPCOnServerOperationAdded_Implementation(const int OperationID, const FInstancedStruct Operation)
{
	UE_LOG(LogTemp, Warning, TEXT("RPCOnServerOperationAdded: %d"), OperationID);
	BoundQueueV2.CacheOperationPayload(OperationID, Operation);
	BoundQueueV2.ClientQueuedOperations.Add(OperationID);
}

void UGMC_AbilitySystemComponent::BoundQueueV2Debug(TSubclassOf<UGMCAbilityEffect> Effect)
{
	if (HasAuthority())
	{
		FGMASBoundQueueV2ApplyEffectOperation EffectActivationData;
		EffectActivationData.EffectClass = Effect;
		EffectActivationData.EffectID = GetNextAvailableServerAuthEffectID();
		int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(EffectActivationData);
		BoundQueueV2.QueueServerOperation(OperationID);
	}
} 

void UGMC_AbilitySystemComponent::OnServerOperationForced(FInstancedStruct OperationData)
{
	UE_LOG(LogTemp, Warning, TEXT("Forcing Operation On Server"));
	ProcessOperation(OperationData, false, true);
}

void UGMC_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();

	InitializeStartingAbilities();
	InitializeAbilityMap();
	SetStartingTags();

	// Bind to Queue Events
	BoundQueueV2.OnServerOperationAdded.AddDynamic(this, &UGMC_AbilitySystemComponent::RPCOnServerOperationAdded);
	BoundQueueV2.OnServerOperationForced.AddDynamic(this, &UGMC_AbilitySystemComponent::OnServerOperationForced);

	// Owning client pulls a snapshot of every server-tracked effect so a kept-pawn
	// reconnect (server reuses Pawn -> ASC, bStartingEffectsApplied stays true,
	// no per-effect RPC re-broadcast to the new connection) doesn't leave us
	// without local UGMCAbilityEffect instances. Server replies with all live
	// effects; client skips IDs already present, so brand-new logins (where the
	// standard apply RPC has already created the locals) are no-ops.
	//
	// Deferred via TryRequestActiveEffectsSnapshot because at component-BeginPlay
	// the owner role / owning connection / locally-controlled state are racy
	// against the actor channel open + Possess pipeline. Firing too early
	// drops the Server RPC silently with no log.
	if (!HasAuthority())
	{
		TryRequestActiveEffectsSnapshot();
	}
}

void UGMC_AbilitySystemComponent::InstantiateAttributes()
{
	BoundAttributes = FGMCAttributeSet();
	UnBoundAttributes = FGMCUnboundAttributeSet();
	OldUnBoundAttributes = FGMCUnboundAttributeSet();
	if(AttributeDataAssets.IsEmpty()) return;

	// Loop through each of the data assets inputted into the component to create new attributes.
	for(UGMCAttributesData* AttributeDataAsset : AttributeDataAssets){

		// Avoid crashing in an editor preview if we're actually editing the ability component's attribute table.
		if (!AttributeDataAsset) continue;
		
		for(const FAttributeData AttributeData : AttributeDataAsset->AttributeData){
			FAttribute NewAttribute;
			NewAttribute.Tag = AttributeData.AttributeTag;
			NewAttribute.InitialValue = AttributeData.DefaultValue;
			SetAttributeInitialValue(NewAttribute.Tag, NewAttribute.InitialValue);
			NewAttribute.Clamp = AttributeData.Clamp;
			NewAttribute.Clamp.AbilityComponent = this;
			NewAttribute.bIsGMCBound = AttributeData.bGMCBound;
			NewAttribute.bStartFull = AttributeData.bStartFull;
			NewAttribute.ValueCombineMode = AttributeData.ValueCombineMode;
			NewAttribute.Init();
			
			if(AttributeData.bGMCBound){
				BoundAttributes.AddAttribute(NewAttribute);
			}
			else {
				// Initiate old unbound attributes
				UnBoundAttributes.AddAttribute(NewAttribute);
				OldUnBoundAttributes.AddAttribute(NewAttribute);
			}
		}
	}

	// After all attributes are initialized, calc their values which will primarily apply their Clamps
	
	for (const FAttribute& Attribute : BoundAttributes.Attributes)
	{
		Attribute.Init();
	}

	for (FAttribute& Attribute : UnBoundAttributes.Items)
	{
		Attribute.Init();
		UnBoundAttributes.MarkItemDirty(Attribute);
	}
	
	for (const FAttribute& Attribute : OldUnBoundAttributes.Items)
	{
		Attribute.Init();
	}
	
	OldBoundAttributes = BoundAttributes;
}

void UGMC_AbilitySystemComponent::SetStartingTags()
{
	ActiveTags.AppendTags(StartingTags);
}

void UGMC_AbilitySystemComponent::CheckActiveTagsChanged()
{
	// Only bother checking changes in tags if we actually have delegates which care.
	if (OnActiveTagsChanged.IsBound() || !FilteredTagDelegates.IsEmpty())
	{
		if (GetActiveTags() != PreviousActiveTags)
		{
			FGameplayTagContainer AllTags = GetActiveTags();
			AllTags.AppendTags(PreviousActiveTags);
		
			FGameplayTagContainer AddedTags;
			FGameplayTagContainer RemovedTags;
			for (const FGameplayTag& Tag : AllTags)
			{
				if (GetActiveTags().HasTagExact(Tag) && !PreviousActiveTags.HasTagExact(Tag))
				{
					AddedTags.AddTagFast(Tag);
				}
				else if (!GetActiveTags().HasTagExact(Tag) && PreviousActiveTags.HasTagExact(Tag))
				{
					RemovedTags.AddTagFast(Tag);
				}
			}
			
			// Let any general 'active tag changed' delegates know about our changes.
			OnActiveTagsChanged.Broadcast(AddedTags, RemovedTags);

			// If we have filtered tag delegates, call them if appropriate.
			if (!FilteredTagDelegates.IsEmpty())
			{
				for (const auto& FilteredBinding : FilteredTagDelegates)
				{
					FGameplayTagContainer AddedMatches = AddedTags.Filter(FilteredBinding.Key);
					FGameplayTagContainer RemovedMatches = RemovedTags.Filter(FilteredBinding.Key);

					if (!AddedMatches.IsEmpty() || !RemovedMatches.IsEmpty())
					{
						FilteredBinding.Value.Broadcast(AddedMatches, RemovedMatches);
					}
					
				}
			}
		
			PreviousActiveTags = GetActiveTags();
		}
	}
}


void UGMC_AbilitySystemComponent::CheckAttributeChanged() {
	// Check Bound Attributes
	for (int i = 0; i < BoundAttributes.Attributes.Num(); i++)
	{
		FAttribute& Attribute = BoundAttributes.Attributes[i];
		FAttribute& OldAttribute = OldBoundAttributes.Attributes[i];
		if (Attribute.Value != OldAttribute.Value)
		{
			NativeAttributeChangeDelegate.Broadcast(Attribute.Tag, OldAttribute.Value, Attribute.Value);
			OnAttributeChanged.Broadcast(Attribute.Tag, OldAttribute.Value, Attribute.Value);
			OldAttribute.Value = Attribute.Value;
		}
	}
}


void UGMC_AbilitySystemComponent::NoteAbilityEnded(int AbilityID)
{
	// Bounded FIFO: drop the oldest once at capacity so this never grows unbounded over a match.
	RecentlyEndedAbilityIDs.Remove(AbilityID);
	RecentlyEndedAbilityIDs.Add(AbilityID);
	if (RecentlyEndedAbilityIDs.Num() > RecentlyEndedAbilityIDsCapacity)
	{
		RecentlyEndedAbilityIDs.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

void UGMC_AbilitySystemComponent::CleanupStaleAbilities()
{
	for (auto It = ActiveAbilities.CreateIterator(); It; ++It)
	{
		// If the contained ability is in the Ended state, delete it
		if (It.Value()->AbilityState == EAbilityState::Ended)
		{
			// Remember this ID so a still-in-flight client heartbeat for it reads as a benign
			// end-race (Verbose) in RPCTaskHeartbeat, not a phantom-divergence Warning.
			NoteAbilityEnded(It.Value()->GetAbilityID());
			if (HasAuthority() && !GMCMovementComponent->IsLocallyControlledServerPawn())
			{
				// Fail safe to tell client server has ended the ability
				RPCClientEndAbility(It.Value()->GetAbilityID());
			};
			It.RemoveCurrent();
		}
	}
}

void UGMC_AbilitySystemComponent::TickActiveEffects(float DeltaTime)
{
	// Skip during replay: ProcessedEffectIDs / ActiveEffects don't rewind, so mutating
	// them off transient rewound bound-state snapshots reaps live effects mid-replay.
	const bool bIsReplaying = IsReplayingForGMASLogic();

	// Monotonic Pending → Validated promotion. Demotion is intentionally absent —
	// bound-state replication has natural latency, and inferring removal from
	// bound absence would reap legitimate effects during the round-trip window.
	// Server-explicit removal goes through RPCClientEndEffect; server-rejected
	// predicts age out via the Pending+timeout reap below.
	if (!HasAuthority() && !bIsReplaying)
	{
		// Snapshot the keys before iterating: EndEffect() below can re-enter this map via
		// BP-side handlers and gameplay-tag listeners (RemoveTags / OnEffectEnded broadcasts
		// that ultimately call back into the ASC). Iterating the live TMap while mutations
		// land on its underlying TSparseArray invalidates the iterator — symptom is either
		// an out-of-bounds crash, or silently skipped Pending→Validated promotions which
		// then surface as attribute drift on reconnect (when a bound-state burst fills
		// many Pending entries at once and several have PendingReplacements queued).
		TArray<int32> KeysSnapshot;
		ProcessedEffectIDs.GenerateKeyArray(KeysSnapshot);

		for (const int32 Key : KeysSnapshot)
		{
			EGMCEffectAnswerState* StatePtr = ProcessedEffectIDs.Find(Key);
			if (!StatePtr) continue;  // entry was removed by a re-entrant callback — skip safely

			if (*StatePtr == EGMCEffectAnswerState::Pending
				&& BoundActiveEffectIDs_Contains(Key))
			{
				*StatePtr = EGMCEffectAnswerState::Validated;

				// Successor confirmed by server → finalize the suspended OLDs.
				// Real EndEffect runs now (tag cleanup, modifier rollback, etc.) so the
				// ability-cleanup pathways downstream see a properly-ended effect.
				if (TArray<TWeakObjectPtr<UGMCAbilityEffect>>* Suspended = PendingReplacements.Find(Key))
				{
					for (const TWeakObjectPtr<UGMCAbilityEffect>& Old : *Suspended)
					{
						if (UGMCAbilityEffect* OldRaw = Old.Get())
						{
							if (!OldRaw->bCompleted)
							{
								OldRaw->bPendingDeathBySuccessor = false;
								OldRaw->EndAtActionTimer = -1.0;
								OldRaw->EndEffect();
							}
						}
					}
					PendingReplacements.Remove(Key);
				}
			}
		}
	}

	TArray<int> CompletedActiveEffects;

	// Tick Effects.
	// Snapshot keys before iterating: Effect.Tick / EndEffect can re-enter this map
	// via BP-side handlers, OnEffectApplied/OnEffectRemoved broadcasts, or gameplay-tag
	// listeners that ultimately call ApplyAbilityEffect/RemoveActiveAbilityEffect.
	// Iterating the live TMap while mutations land on its underlying TSparseArray
	// invalidates the iterator (ensures fire in SparseArray.h, then crash).
	// Mirror the snapshot pattern used for ProcessedEffectIDs above.
	TArray<int> ActiveEffectKeys;
	ActiveEffects.GenerateKeyArray(ActiveEffectKeys);
	for (const int Key : ActiveEffectKeys)
	{
		UGMCAbilityEffect** EffectPtr = ActiveEffects.Find(Key);
		if (!EffectPtr || !*EffectPtr)
		{
			// Entry was removed by a re-entrant callback during a previous iteration.
			continue;
		}
		UGMCAbilityEffect* EffectValue = *EffectPtr;

		if (!EffectValue->IsValidLowLevel()) {
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Active Effect id %d is null or pending kill, removing from the list."), Key);
			CompletedActiveEffects.Push(Key);
			continue;
		}

		EffectValue->Tick(DeltaTime);
		if (EffectValue->bCompleted)
		{
			CompletedActiveEffects.Push(Key);
		}

		// Check for predicted effects that have not been server confirmed.
		// Skip during replay — same reason as the polling block above: we do NOT want
		// replay to mutate persistent state (ProcessedEffectIDs / ActiveEffects) based
		// on transient rewound bound state. Reaps run on fresh post-replay state only.
		if (!HasAuthority() && !bIsReplaying &&
			!EffectValue->EffectData.bServerAuth
			&& ProcessedEffectIDs.Contains(Key)
			&& ProcessedEffectIDs[Key] == EGMCEffectAnswerState::Pending
			&& EffectValue->ClientEffectApplicationTime + GetDefault<UGMASNetworkTimingSettings>()->ClientEffectApplicationTimeout < ActionTimer)
		{
			ProcessedEffectIDs[Key] = EGMCEffectAnswerState::Timeout;
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Effect `%s` Not Confirmed By Server (ID: `%d`), Removing..."), *GetNameSafe(EffectValue), Key);

			// Successor rejected by server → revive the suspended OLDs. They resume
			// ticking modifiers from the next tick onward; their natural EndAtActionTimer
			// (still set, never cleared in the suspend path) will fire when due.
			if (TArray<TWeakObjectPtr<UGMCAbilityEffect>>* Suspended = PendingReplacements.Find(Key))
			{
				for (const TWeakObjectPtr<UGMCAbilityEffect>& Old : *Suspended)
				{
					if (UGMCAbilityEffect* OldRaw = Old.Get())
					{
						if (!OldRaw->bCompleted)
						{
							OldRaw->bPendingDeathBySuccessor = false;
						}
					}
				}
				PendingReplacements.Remove(Key);
			}

			EffectValue->EndEffect();
			CompletedActiveEffects.Push(Key);
		}
	}

	// Clean expired effects
	for (const int EffectID : CompletedActiveEffects)
	{
		// Notify client. Redundant.
		if (HasAuthority()) {RPCClientEndEffect(EffectID);}

		// Revive any OLDs still suspended on this successor — covers cleanup paths
		// that bypass the Validated/Timeout transitions.
		if (TArray<TWeakObjectPtr<UGMCAbilityEffect>>* Suspended = PendingReplacements.Find(EffectID))
		{
			for (const TWeakObjectPtr<UGMCAbilityEffect>& Old : *Suspended)
			{
				if (UGMCAbilityEffect* OldRaw = Old.Get())
				{
					if (!OldRaw->bCompleted)
					{
						OldRaw->bPendingDeathBySuccessor = false;
					}
				}
			}
			PendingReplacements.Remove(EffectID);
		}

		ActiveEffects.Remove(EffectID);
		ProcessedEffectIDs.Remove(EffectID);

		// Symmetric with the Add in ApplyAbilityEffect.
		BoundActiveEffectIDs_Remove(EffectID);
	}

	// Clean effect handles
	TArray<int> CurrentHandles;
	EffectHandles.GetKeys(CurrentHandles);
	for (const int Handle : CurrentHandles)
	{
		const auto& Data = EffectHandles[Handle];
		if (Data.NetworkId > 0 && !ActiveEffects.Contains(Data.NetworkId))
		{
			EffectHandles.Remove(Handle);
		}
	}
	
}

void UGMC_AbilitySystemComponent::ProcessAttributes(bool bInGenPredictionTick)
{
	for (const FAttribute* Attribute : GetAllAttributes())
	{
		if (Attribute->IsDirty() && Attribute->bIsGMCBound == bInGenPredictionTick)
		{
			Attribute->CalculateValue();
			// Broadcast dirty change if unbound
			if (!Attribute->bIsGMCBound)
			{
				UnBoundAttributes.MarkAttributeDirty(*Attribute);
			}
		}
	}
	
}


void UGMC_AbilitySystemComponent::TickActiveAbilities(float DeltaTime)
{
	for (const TPair<int, UGMCAbility*>& Ability : ActiveAbilities)
	{
		Ability.Value->Tick(DeltaTime);
	}
}

void UGMC_AbilitySystemComponent::TickAncillaryActiveAbilities(float DeltaTime){
	for (const TPair<int, UGMCAbility*>& Ability : ActiveAbilities)
	{
		Ability.Value->AncillaryTick(DeltaTime);
	}
}

void UGMC_AbilitySystemComponent::TickActiveCooldowns(float /*DeltaTime*/)
{
	// Cooldowns store absolute expiry times (ActionTimer units), not
	// remaining durations — see ActiveCooldowns declaration. This is
	// purely a garbage-collection pass for entries whose expiry has
	// passed. DeltaTime is intentionally ignored: ticking this multiple
	// times per real frame (which GMC's combined-move re-execution does)
	// no longer accumulates drift, because expiry is a fixed point in
	// time, not a counter being decremented.
	for (auto It = ActiveCooldowns.CreateIterator(); It; ++It)
	{
		if (It.Value() <= ActionTimer)
		{
			It.RemoveCurrent();
		}
	}
}

bool UGMC_AbilitySystemComponent::IsLocallyControlledPawnASC() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Pawn->IsLocallyControlled();
	}

	return false;
}

void UGMC_AbilitySystemComponent::RPCClientEndEffect_Implementation(int EffectID)
{
	if (ActiveEffects.Contains(EffectID))
	{
		// Route through RemoveActiveAbilityEffect so the bilateral defer (Bug #3) applies here too.
		// Server's local RemoveActiveAbilityEffect armed its own defer; client must arm a matching one
		// to keep per-tick application counts symmetric on Ticking / Periodic effects.
		RemoveActiveAbilityEffect(ActiveEffects[EffectID]);
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[RPC] Server Ended Effect: %d"), EffectID);
	}
}

void UGMC_AbilitySystemComponent::RPCTaskHeartbeat_Implementation(int AbilityID, int TaskID)
{
	if (ActiveAbilities.Contains(AbilityID) && ActiveAbilities[AbilityID] != nullptr)
	{
		ActiveAbilities[AbilityID]->HandleTaskHeartbeat(TaskID);
	}
	else if (WasAbilityRecentlyEnded(AbilityID))
	{
		// Benign end-race: the server HAD this ability and already ended it (e.g. a short
		// client-predicted weapon-raise whose client instance outlives the server twin by a
		// tick, long enough to fire its one immediate heartbeat). The server's RPCClientEndAbility
		// is already on its way back. Not a divergence — keep it quiet. Mirrors the TaskID-level
		// benign-end-race branch in UGMCAbility::HandleTaskHeartbeat.
		UE_LOG(LogGMCAbilitySystem, Verbose,
			TEXT("[TaskDiag] Heartbeat for already-ended AbilityID=%d (TaskID=%d) ignored (benign end race)."),
			AbilityID, TaskID);
	}
	else
	{
		// [TaskDiag] probe: the client is heartbeating an AbilityID the server NEVER had (not in
		// the recently-ended ring either). A genuine activation divergence — the client predicted
		// an activation that never produced a server instance. List our live IDs for comparison.
		// Throttled naturally by the 1/s per-task client send rate.
		FString LiveIDs;
		for (const auto& Pair : ActiveAbilities)
		{
			LiveIDs += FString::Printf(TEXT("%d(%s) "), Pair.Key, Pair.Value ? *Pair.Value->AbilityTag.ToString() : TEXT("null"));
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[TaskDiag] Heartbeat for unknown AbilityID=%d (TaskID=%d) — client ability alive but server has no such instance. Server live abilities: %s"),
			AbilityID, TaskID, *LiveIDs);
	}
}

void UGMC_AbilitySystemComponent::RPCClientEndAbility_Implementation(int AbilityID)
{
	if (ActiveAbilities.Contains(AbilityID))
	{
		// [AbilityCut] probe: the server force-ends an ability that is still ACTIVE on this
		// client — the player-visible "it cut by itself" moment (e.g. server watchdog kill).
		// An RPC for an already-Ended local instance is routine cleanup and stays quiet.
		UGMCAbility* LocalAbility = ActiveAbilities[AbilityID];
		if (LocalAbility && LocalAbility->AbilityState != EAbilityState::Ended)
		{
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[AbilityCut] Server force-ended ability that was still active locally. %s"),
				*LocalAbility->GetAbilityCutDiagnostics());
		}
		ActiveAbilities[AbilityID]->EndAbility();
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[RPC] Server Ended Ability: %d"), AbilityID);
	}
}

void UGMC_AbilitySystemComponent::RPCConfirmAbilityActivation_Implementation(int AbilityID)
{
	if (ActiveAbilities.Contains(AbilityID))
	{
		ActiveAbilities[AbilityID]->ServerConfirm();
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[RPC] Server Confirmed Long-Running Ability Activation: %d"), AbilityID);
	}
}


void UGMC_AbilitySystemComponent::ApplyStartingEffects(bool bForce) {
	if (!HasAuthority() || StartingEffects.Num() == 0 || (!bForce && bStartingEffectsApplied))
	{
		return;
	}

	// Defer until the owner pawn is actually controlled. ApplyAbilityEffectShort with
	// ServerAuth pushes RPCOnServerOperationAdded to the owning client; if we fire it
	// before PossessedBy completes, no relevant connection exists yet and the RPC is
	// dropped silently and the client never gets the local instance — bound attributes
	// drift and there is no signal to reapply. Gating on Controller is the smallest
	// safe condition: it is non-null only after PossessedBy has finished server-side,
	// which is exactly when the owning connection is established.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn || !OwnerPawn->GetController())
	{
		return;
	}

	for (const TSubclassOf<UGMCAbilityEffect>& Effect : StartingEffects)
	{
		// Dont apply the same effect twice
		if (!Algo::FindByPredicate(ActiveEffects, [Effect](const TPair<int, UGMCAbilityEffect*>& ActiveEffect) {
			return IsValid(ActiveEffect.Value) && ActiveEffect.Value->GetClass() == Effect;
		})) {
			ApplyAbilityEffectShort(Effect, EGMCAbilityEffectQueueType::ServerAuth);
		}
	}
	bStartingEffectsApplied = true;
}


void UGMC_AbilitySystemComponent::TryRequestActiveEffectsSnapshot()
{
	if (HasAuthority() || bSnapshotRequestSent)
	{
		return;
	}

	AActor* Owner = GetOwner();
	const APawn* OwnerPawn = Cast<APawn>(Owner);

	// Both gates required:
	//  - IsLocallyControlled: filters out simulated-proxy ASCs (we'd never
	//    succeed sending a Server RPC on someone else's pawn anyway).
	//  - LocalRole == AutonomousProxy: ensures the role bit has propagated
	//    server -> client AND the owning connection is registered, both of
	//    which are prerequisites for the RPC to actually leave the wire.
	const bool bReady = IsValid(OwnerPawn)
		&& OwnerPawn->IsLocallyControlled()
		&& Owner->GetLocalRole() == ROLE_AutonomousProxy;

	if (!bReady)
	{
		// Bounded retry. Either the gate opens within a couple of seconds
		// (race between BeginPlay and Possess→AcknowledgePossession) or
		// this ASC belongs to a simulated proxy and will never qualify.
		if (++SnapshotRequestRetryCount > MaxSnapshotRequestRetries)
		{
			return;
		}

		if (UWorld* World = GetWorld())
		{
			FTimerHandle UnusedHandle;
			World->GetTimerManager().SetTimerForNextTick(
				FTimerDelegate::CreateUObject(this, &UGMC_AbilitySystemComponent::TryRequestActiveEffectsSnapshot));
		}
		return;
	}

	UE_LOG(LogGMCAbilitySystem, Verbose,
		TEXT("[ReconnectSnapshot] Sending Server_RequestActiveEffectsSnapshot retries=%d owner=%s"),
		SnapshotRequestRetryCount,
		*GetNameSafe(Owner));

	Server_RequestActiveEffectsSnapshot();
	bSnapshotRequestSent = true;
}


bool UGMC_AbilitySystemComponent::Server_RequestActiveEffectsSnapshot_Validate()
{
	// Owning-client request, no payload to validate. Always accept.
	return true;
}

void UGMC_AbilitySystemComponent::Server_RequestActiveEffectsSnapshot_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<FGMCEffectSnapshot> Snapshots;
	BuildActiveEffectsSnapshot(Snapshots);
	if (Snapshots.Num() > 0)
	{
		Client_ReceiveActiveEffectsSnapshot(Snapshots);
	}
}

void UGMC_AbilitySystemComponent::Client_ReceiveActiveEffectsSnapshot_Implementation(const TArray<FGMCEffectSnapshot>& Snapshots)
{
	for (const FGMCEffectSnapshot& Snapshot : Snapshots)
	{
		if (ActiveEffects.Contains(Snapshot.EffectID))
		{
			// Already created locally (e.g. by the standard apply RPC for a brand-
			// new login). Snapshot is informational only in that case.
			continue;
		}
		RestoreEffectFromSnapshot(Snapshot);
	}
}

void UGMC_AbilitySystemComponent::BuildActiveEffectsSnapshot(TArray<FGMCEffectSnapshot>& OutSnapshots) const
{
	OutSnapshots.Reset();
	OutSnapshots.Reserve(ActiveEffects.Num());

	for (const TPair<int, UGMCAbilityEffect*>& Pair : ActiveEffects)
	{
		UGMCAbilityEffect* Effect = Pair.Value;
		if (!IsValid(Effect) || Effect->bCompleted)
		{
			continue;
		}

		FGMCEffectSnapshot Snapshot;
		Snapshot.EffectClass = Effect->GetClass();
		Snapshot.EffectID = Effect->EffectData.EffectID;
		// StartTime is in server's ActionTimer space. Send the elapsed delta so
		// the client can rebuild StartTime in its own local clock — preserves
		// periodic-tick boundary alignment and remaining duration without
		// exposing the server's absolute clock.
		Snapshot.TimeSinceStart = ActionTimer - Effect->EffectData.StartTime;
		Snapshot.Duration = Effect->EffectData.Duration;
		Snapshot.bServerAuth = Effect->EffectData.bServerAuth;
		Snapshot.EffectTag = Effect->EffectData.EffectTag;
		OutSnapshots.Add(MoveTemp(Snapshot));
	}
}

void UGMC_AbilitySystemComponent::RestoreEffectFromSnapshot(const FGMCEffectSnapshot& Snapshot)
{
	if (!Snapshot.EffectClass)
	{
		return;
	}

	UGMCAbilityEffect* Effect = DuplicateObject(
		Snapshot.EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
	if (!Effect)
	{
		return;
	}

	// Start from the CDO's data (recovered through DuplicateObject), then overlay
	// snapshot fields. Mirrors what ApplyAbilityEffect does in the standard path,
	// minus the bUniqueByEffectTag rejection (snapshot entries are pre-validated
	// authoritatively on the server).
	FGMCAbilityEffectData InitData = Effect->EffectData;
	InitData.OwnerAbilityComponent = this;
	InitData.SourceAbilityComponent = this;
	InitData.EffectID = Snapshot.EffectID;
	InitData.bServerAuth = Snapshot.bServerAuth;
	InitData.EffectTag = Snapshot.EffectTag;
	InitData.Duration = Snapshot.Duration;

	// Remap server elapsed time to the client's local ActionTimer so periodic
	// boundaries and remaining duration line up. InitializeEffect takes the
	// (StartTime != 0) branch when StartTime is provided here, so 0.0 must be
	// avoided at the exact frame boundary — extremely unlikely given ActionTimer
	// is a continuously-advancing double, but bias toward a tiny epsilon if it
	// ever lands on zero.
	const double LocalStart = ActionTimer - Snapshot.TimeSinceStart;
	InitData.StartTime = (LocalStart != 0.0) ? LocalStart : KINDA_SMALL_NUMBER;
	InitData.EndTime = (InitData.Duration > 0.0)
		? InitData.StartTime + InitData.Duration
		: 0.0;

	Effect->InitializeEffect(InitData);

	ActiveEffects.Add(InitData.EffectID, Effect);
	// Snapshot effects are already server-authoritative — skip the Pending state
	// the standard client predict path uses; mark Validated directly so the
	// Pending->Validated promotion machinery doesn't try to confirm them.
	ProcessedEffectIDs.Add(InitData.EffectID, EGMCEffectAnswerState::Validated);

	UE_LOG(LogGMCAbilitySystem, Verbose,
		TEXT("[ReconnectSnapshot] Restored effect %s id=%d t_since_start=%.3f duration=%.3f local_start=%.3f"),
		*GetNameSafe(Snapshot.EffectClass),
		InitData.EffectID,
		Snapshot.TimeSinceStart,
		Snapshot.Duration,
		InitData.StartTime);
}


TArray<TSubclassOf<UGMCAbility>> UGMC_AbilitySystemComponent::GetGrantedAbilitiesByTag(FGameplayTag AbilityTag)
{
	if (!GrantedAbilityTags.HasTag(AbilityTag))
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Tag Not Granted: %s"), *AbilityTag.ToString());
		return {};
	}

	if (!AbilityMap.Contains(AbilityTag))
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Tag Not Found: %s | Check The Component's AbilityMap"), *AbilityTag.ToString());
		return {};
	}

	return AbilityMap[AbilityTag].Abilities;
}


void UGMC_AbilitySystemComponent::ClearAbilityAndTaskData() {
	AbilityData = FGMCAbilityData{};
	// QueuedAbilityOperations.ClearCurrentOperation();
	TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});
}


void UGMC_AbilitySystemComponent::SendTaskDataToActiveAbility(bool bFromMovement) {

	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.IsValid() ? TaskData.Get<FGMCAbilityTaskData>() : FGMCAbilityTaskData{};
	if (TaskDataFromInstance != FGMCAbilityTaskData{} && /*safety check*/ TaskDataFromInstance.TaskID >= 0)
	{
		// FindRef-hoist: a GC-nulled map VALUE would pass Contains() and crash the deref chain;
		// resolve once, null-safe, and reuse below.
		UGMCAbility* TargetAbility = ActiveAbilities.FindRef(TaskDataFromInstance.AbilityID);
		if (TargetAbility && TargetAbility->bActivateOnMovementTick == bFromMovement)
		{
			// [TaskDiag] probe: TaskData is GMC-bound as ClientAuth_Input, so a client replay
			// restores the historical payload of every replayed move and re-enters this
			// dispatch. With finished tasks unregistering themselves, a replayed payload for an
			// already-unregistered task is the NORMAL replay outcome (ignored downstream) —
			// only a replayed dispatch that will reach a LIVE task risks re-running its BP
			// continuation (double Completed broadcast). Keep that case loud, quiet the rest.
			if (IsReplayingForGMASLogic())
			{
				if (TargetAbility->RunningTasks.FindRef(TaskDataFromInstance.TaskID) != nullptr)
				{
					UE_LOG(LogGMCAbilitySystem, Warning,
						TEXT("[TaskDiag] Progress payload RE-dispatched during replay (AbilityID=%d TaskID=%d fromMovement=%d). %s"),
						TaskDataFromInstance.AbilityID, TaskDataFromInstance.TaskID, bFromMovement ? 1 : 0,
						*TargetAbility->GetAbilityCutDiagnostics());
				}
				else
				{
					UE_LOG(LogGMCAbilitySystem, Verbose,
						TEXT("[TaskDiag] Replayed Progress payload for already-unregistered TaskID=%d (AbilityID=%d) — benign, ignored downstream."),
						TaskDataFromInstance.TaskID, TaskDataFromInstance.AbilityID);
				}
			}
			TargetAbility->HandleTaskData(TaskDataFromInstance.TaskID, TaskData);
		}
		else if (!ActiveAbilities.Contains(TaskDataFromInstance.AbilityID))
		{
			// [TaskDiag] probe: a valid Progress payload addressed an AbilityID this side does
			// not have — the payload is silently lost and the addressed task (on the sender's
			// side or on our twin instance) will never progress. Fires from both the movement
			// and ancillary dispatch points, so expect up to two lines per lost payload.
			// During replay this can also be a historical payload for an already-cleaned
			// ability — the Replaying flag in the message separates the two cases.
			FString LiveIDs;
			for (const auto& Pair : ActiveAbilities)
			{
				LiveIDs += FString::Printf(TEXT("%d(%s) "), Pair.Key, Pair.Value ? *Pair.Value->AbilityTag.ToString() : TEXT("null"));
			}
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[TaskDiag] Progress payload lost: AbilityID=%d (TaskID=%d) not in ActiveAbilities (fromMovement=%d Authority=%d Replaying=%d). Live abilities: %s"),
				TaskDataFromInstance.AbilityID, TaskDataFromInstance.TaskID, bFromMovement ? 1 : 0,
				HasAuthority() ? 1 : 0, IsReplayingForGMASLogic() ? 1 : 0, *LiveIDs);
			if (HasAuthority())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[TaskDiag] Progress payload lost: AbilityID=%d (TaskID=%d) not in ActiveAbilities (fromMovement=%d). Live abilities: %s"),
					TaskDataFromInstance.AbilityID, TaskDataFromInstance.TaskID, bFromMovement ? 1 : 0, *LiveIDs);
			}
		}
	}
}


bool UGMC_AbilitySystemComponent::CheckActivationTags(const UGMCAbility* Ability) const {

	if (!Ability) return false;

	// Required Tags
	for (const FGameplayTag Tag : Ability->ActivationRequiredTags)
	{
		if (!HasActiveTag(Tag))
		{
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability can't activate, missing required tag:  %s"), *Tag.ToString());
			return false;
		}
	}


	// Blocking Tags
	for (const FGameplayTag Tag : Ability->ActivationBlockedTags)
	{
		if (HasActiveTag(Tag))
		{
			UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability can't activate, blocked by tag: %s"), *Tag.ToString());
			return false;
		}
	}

	// single activation query
	if (!Ability->ActivationQuery.IsEmpty() && !Ability->ActivationQuery.Matches(ActiveTags))
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability can't activate, blocked by query: %s"),
			*Ability->ActivationQuery.GetDescription());
		return false;
	}

	return true;
}

bool UGMC_AbilitySystemComponent::CheckActivationTagsForClientAuth(const UGMCAbility* Ability) const {
	if (!Ability) return false;

	// BlockedByOtherAbility — runtime coherence (NOT skipped).
	for (const FGameplayTag& BlockTag : Ability->BlockedByOtherAbility)
	{
		if (IsAbilityTagBlocked(BlockTag))
		{
			return false;
		}
	}

	// Cooldown — runtime coherence.
	if (GetCooldownForAbility(Ability->AbilityTag) > 0.f)
	{
		return false;
	}

	// Skipped on purpose (client-auth opt-out):
	//   - ActivationRequiredTags
	//   - ActivationBlockedTags
	return true;
}

bool UGMC_AbilitySystemComponent::TryActivateClientAuthAbility(
	TSubclassOf<UGMCAbility> AbilityClass,
	FGameplayTag InputTag,
	const UInputAction* InputAction)
{
	if (!AbilityClass) return false;

	UGMCAbility* CDO = AbilityClass->GetDefaultObject<UGMCAbility>();
	if (!CheckActivationTagsForClientAuth(CDO))
	{
		return false;
	}

	// Server-owned pawn? no client->server payload to send, and no remote twin to pair
	// with — local ActionTimer-based ID generation is fine.
	if (HasAuthority())
	{
		return TryActivateAbility(AbilityClass, InputAction, InputTag,
			/*bSkipActivationTagsCheck=*/true);
	}

	// Build the operation FIRST so the local activation can use the operation-derived
	// AbilityID — the server's handler derives the same ID from Op.OperationID, which is
	// what pairs the two instances (heartbeats and task payloads address by AbilityID;
	// client-auth has no RPCConfirmAbilityActivation to paper over a mismatch).
	FGMASBoundQueueV2ClientAuthAbilityActivationOperation Op;
	Op.AbilityClass = AbilityClass;
	Op.InputTag = InputTag;
	Op.InputAction = InputAction;
	const int OpID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ClientAuthAbilityActivationOperation>(Op);

	// Local activation -- bSkipActivationTagsCheck=true because
	// CheckActivationTagsForClientAuth already enforced the reduced gate set.
	const bool bActivated = TryActivateAbility(AbilityClass, InputAction, InputTag,
		/*bSkipActivationTagsCheck=*/true,
		DeriveAbilityIDFromOperation(OpID, 0));
	if (!bActivated)
	{
		// Don't ship an operation whose local activation failed; drop its cached payload.
		BoundQueueV2.RemovePayloadByID(OpID);
		return false;
	}

	BoundQueueV2.QueueClientOperation(OpID);
	return true;
}


void UGMC_AbilitySystemComponent::ClearAbilityMap()
{
	// For each AbilityMap in the map AbilityMap:
	for (auto& AbilityMapData : AbilityMap)
	{
		if (GrantedAbilityTags.HasTag(AbilityMapData.Value.InputTag))
		{
			GrantedAbilityTags.RemoveTag(AbilityMapData.Value.InputTag);
		}
	}
	
	AbilityMap.Empty();
}

void UGMC_AbilitySystemComponent::SetAttributeInitialValue(const FGameplayTag& AttributeTag, float& BaseValue)
{
	OnInitializeAttributeInitialValue(AttributeTag, BaseValue);
}

void UGMC_AbilitySystemComponent::InitializeAbilityMap(){
	for (UGMCAbilityMapData* StartingAbilityMap : AbilityMaps)
	{

		// Avoid crashing if we're adding a new entry to the ability map in the editor.
		if (!StartingAbilityMap) continue;
		
		for (const FAbilityMapData& Data : StartingAbilityMap->GetAbilityMapData())
		{
			AddAbilityMapData(Data);
		}
	}
}

void UGMC_AbilitySystemComponent::AddAbilityMapData(const FAbilityMapData& AbilityMapData)
{
	if (AbilityMap.Contains(AbilityMapData.InputTag))
	{
		AbilityMap[AbilityMapData.InputTag] = AbilityMapData;
	}
	else
	{
		AbilityMap.Add(AbilityMapData.InputTag, AbilityMapData);
	}
	
	if (AbilityMapData.bGrantedByDefault)
	{
		GrantedAbilityTags.AddTag(AbilityMapData.InputTag);
	}
}

void UGMC_AbilitySystemComponent::RemoveAbilityMapData(const FAbilityMapData& AbilityMapData)
{
	if (AbilityMap.Contains(AbilityMapData.InputTag))
	{
		AbilityMap.Remove(AbilityMapData.InputTag);
	}
	
	if (GrantedAbilityTags.HasTag(AbilityMapData.InputTag))
	{
		GrantedAbilityTags.RemoveTag(AbilityMapData.InputTag);
	}
	
}

void UGMC_AbilitySystemComponent::InitializeStartingAbilities()
{
	for (FGameplayTag Tag : StartingAbilities)
	{
		GrantedAbilityTags.AddTag(Tag);
	}
}

void UGMC_AbilitySystemComponent::ExecuteSyncedEvent(FGMASSyncedEventContainer EventData)
{
	if (!HasAuthority())
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Client attempted to create a SyncedEvent"));
		return;
	}

	if (EventData.EventTag == FGameplayTag::EmptyTag)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Attempted to create a SyncedEvent with an empty tag"));
		return;
	}

	EventData.EventType = EGMASSyncedEventType::BlueprintImplemented;
	
	// TGMASBoundQueueOperation<UGMASSyncedEvent, FGMASSyncedEventContainer> Operation;	
	// if (CreateSyncedEventOperation(Operation, EventData) == -1 )
	// {
	// 	UE_LOG(LogGMCAbilitySystem, Error, TEXT("Failed to create SyncedEvent"));
	// 	return;
	// }
	
	// ClientQueueOperation(Operation);
}

bool UGMC_AbilitySystemComponent::ProcessOperation(FInstancedStruct OperationData, bool bFromMovementTick, bool bForce)
{
	if (!BoundQueueV2.IsValidGMASOperation(OperationData)) return false;

	// Batched dispatch: server broadcast N ops in the same client move payload.
	// Recurse into each sub-op via a synthetic base wrapper carrying only the
	// sub-op's OperationID; the inner ProcessOperation looks up the actual
	// payload from OperationPayloads cache. Per-sub-op AckOp writes to
	// OperationData are suppressed via bInBatchDispatch (the slot would race);
	// a single FGMASBoundQueueV2BatchAcknowledgeOperation is written at the end,
	// carrying every successfully-processed sub-op ID.
	//
	// Handled BEFORE the OperationID==0 bail because the wrapper itself carries
	// no OperationID (sub-IDs live in SubOperationIDs).
	if (OperationData.GetScriptStruct() == FGMASBoundQueueV2BatchOperation::StaticStruct())
	{
		const FGMASBoundQueueV2BatchOperation Batch = OperationData.Get<FGMASBoundQueueV2BatchOperation>();
		TArray<int32> AckedIDs;
		AckedIDs.Reserve(Batch.SubOperationIDs.Num());

		BoundQueueV2.bInBatchDispatch = true;
		for (const int32 SubID : Batch.SubOperationIDs)
		{
			if (!BoundQueueV2.HasPayloadByID(SubID))
			{
				// A sub-operation whose payload is missing from the cache (expired, never
				// delivered) cannot be applied on this side. This used to be a SILENT skip:
				// the batch still reported success, the op landed on the other side only,
				// and the resulting state divergence had no trace anywhere. Keep skipping
				// (nothing to apply) and keep it OUT of the ack list — so the server's
				// grace-timeout drain still has a chance to force it — but log it loudly.
				UE_LOG(LogGMCAbilitySystem, Error,
					TEXT("[BatchOp] Sub-operation %d payload missing from cache — NOT applied on this side (batch of %d sub-ops, Authority=%d, Replaying=%d)."),
					SubID, Batch.SubOperationIDs.Num(), HasAuthority() ? 1 : 0, IsReplayingForGMASLogic() ? 1 : 0);
				if (HasAuthority())
				{
					UE_LOG(LogTemp, Error,
						TEXT("[BatchOp] Sub-operation %d payload missing from cache — NOT applied on this side (batch of %d sub-ops)."),
						SubID, Batch.SubOperationIDs.Num());
				}
				continue;
			}

			FGMASBoundQueueV2OperationBaseData Wrapper;
			Wrapper.OperationID = SubID;
			const FInstancedStruct WrappedSub = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(Wrapper);

			if (ProcessOperation(WrappedSub, bFromMovementTick, bForce))
			{
				AckedIDs.Add(SubID);
			}
		}
		BoundQueueV2.bInBatchDispatch = false;

		// Single batch ack on client; on authority no ack to send.
		if (!HasAuthority() && AckedIDs.Num() > 0)
		{
			FGMASBoundQueueV2BatchAcknowledgeOperation Ack;
			Ack.AcknowledgedIDs = MoveTemp(AckedIDs);
			BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2BatchAcknowledgeOperation>(Ack);
			return true;
		}
		return AckedIDs.Num() > 0;
	}

	const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
	const int OperationID = BaseData->OperationID;

	if (OperationID == 0)
	{
		return false; // Empty/Default Operation, Ignore
	}
	
	// Payload data is normally in the cache. During a client replay (CL_ReplayMoves) the OperationPayloads
	// cache is empty (cleared on ack) AND it is NOT rewound — but the bound OperationData carries the full
	// payload for client-initiated ops (single-op path replicates the derived struct; cf. the line below
	// where PayloadData falls back to OperationData). Without this fallback a client-op activation is
	// SKIPPED on replay and its effect is never re-applied -> the replayed move's bound attributes diverge
	// from the server. This is general replay-correctness for any client-op ability (anything that must
	// survive replay has to be a self-contained bound var, not a bound-slot + a non-rewound cache lookup);
	// it is NOT the fix for the walk/sprint-spam replay storm -- that one is move-combine vs server tickrate,
	// fixed in HumanoidMovementComponent::UpdateSpeed via CL_DoNotCombineNextMove. Use the carried payload,
	// scoped tightly (replay + client op + real payload) so the original run, the server, server-broadcast
	// ops (ID>0), acks and batches are untouched.
	const bool bCacheHit = BoundQueueV2.HasPayloadByID(OperationID);
	if (!bCacheHit)
	{
		const bool bIsReplaying    = GMCMovementComponent && GMCMovementComponent->CL_IsReplaying();
		const bool bCarriesPayload = OperationData.GetScriptStruct()
			&& OperationData.GetScriptStruct() != FGMASBoundQueueV2OperationBaseData::StaticStruct();
		if (!(bIsReplaying && bCarriesPayload && OperationID < 0))
		{
			// UE_LOG(LogGMCAbilitySystem, Error, TEXT("OperationID %d not found in OperationPayloads"), OperationID);
			return false;
		}
	}

	// Pull the payload from the cache, or fall back to the carried bound OperationData on replay.
	FInstancedStruct PayloadData = bCacheHit ? BoundQueueV2.GetPayloadByID(OperationID) : OperationData;

	const UScriptStruct* StructType = PayloadData.GetScriptStruct();

	// Activate Ability — handled in any tick context (AncillaryTick or PredictionTick),
	// so consume the cache entry here on the auth side, but only if ability activation succeeds. 
	if (StructType == FGMASBoundQueueV2AbilityActivationOperation::StaticStruct())
	{
		const FGMASBoundQueueV2AbilityActivationOperation Data = PayloadData.Get<FGMASBoundQueueV2AbilityActivationOperation>();
		if (!BoundQueueV2.bInBatchDispatch)
		{
			BoundQueueV2.OperationData  = PayloadData;
		}

		// Diagnostic: log every ability activation that traverses ProcessOperation, including
		// what the server side does with it. Pair with [ServerOpAccept]/[ServerOpDrop] to
		// trace a Sprint activation from client predict → server validation → server apply.
		if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
		{
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[ProcessOp] op=%d activate_ability tag=%s auth=%d fromMove=%d force=%d"),
				OperationID, *Data.InputTag.ToString(),
				HasAuthority() ? 1 : 0, bFromMovementTick ? 1 : 0, bForce ? 1 : 0);
		}
		
		// Thread the operation ID through so AbilityIDs are operation-derived and identical
		// on client and server (Data.OperationID was stamped once by the queueing side).
		const bool bActivated = TryActivateAbilitiesByInputTag(Data.InputTag, Data.InputAction, bFromMovementTick, bForce, Data.OperationID);

		// The ability can fail if it is running on the irrelevant tick, hence we preserve the payload when necessary.
		if (HasAuthority())
		{
			const bool bPreserveForAncillaryTick = !bActivated && bFromMovementTick;
			if (!bPreserveForAncillaryTick)
			{
				BoundQueueV2.RemovePayloadByID(OperationID);
			}
		}

		return bActivated;
	}

	// Everything below happens only during the Prediction tick (or via the forced
	// timeout path with bForce=true). Bail BEFORE consuming the cache entry so the
	// OnServerOperationForced fallback can still find the payload when grace expires —
	// otherwise multiple ServerAuth ApplyEffect calls in the same frame are silently
	// dropped: AncillaryTick pops them via GenPreLocalMoveExecution, lands here with
	// bFromMovementTick=false, used to RemovePayloadByID then return — the grace
	// timeout later finds an empty cache and skips the broadcast. Net effect: only
	// one of N chained ServerAuth applies survives the race.
	if (!bFromMovementTick && !bForce){
		// BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(FGMASBoundQueueV2OperationBaseData{});
		return false;
	}

	// Server only ever processes operations once so it doesn't need them cached.
	// Done after the prediction-tick guard above to keep the timeout fallback viable.
	if (HasAuthority())
	{
		BoundQueueV2.RemovePayloadByID(OperationID);
	}

	///////////// Server-Auth Events
	// Todo: Every event is setting the same AcknowledgedOperation. Could probably pull that out of each event and just set it once at the end.
	//
	// Apply Effect
	if (StructType == FGMASBoundQueueV2ApplyEffectOperation::StaticStruct())
	{
		const FGMASBoundQueueV2ApplyEffectOperation Data = PayloadData.Get<FGMASBoundQueueV2ApplyEffectOperation>();

		// Diagnostic: server-side or client-side Apply Effect op processing. Filtered by class
		// name so we can isolate the Sprint/SprintCost/Recovery investigation without flooding
		// the log with every single effect.
		if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
		{
			const FString Filter = GMASApplyTrace::CVarApplyTraceFilter.GetValueOnGameThread();
			const FString ClassName = Data.EffectClass ? Data.EffectClass->GetName() : TEXT("null");
			if (Filter.IsEmpty() || ClassName.Contains(Filter))
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ProcessOp] op=%d apply_effect class=%s effect_id=%d auth=%d fromMove=%d"),
					OperationID, *ClassName, Data.EffectID,
					HasAuthority() ? 1 : 0, bFromMovementTick ? 1 : 0);
			}
		}

		ProcessEffectApplicationFromOperation(Data);
		if (!HasAuthority() && !BoundQueueV2.bInBatchDispatch)
		{
			// Make an operation to confirm the effect application
			BoundQueueV2.OperationData  = FInstancedStruct::Make<FGMASBoundQueueV2AcknowledgeOperation>(FGMASBoundQueueV2AcknowledgeOperation{OperationID});
		}
		return true;
	}

	// Remove Effect
	if (StructType == FGMASBoundQueueV2RemoveEffectOperation::StaticStruct())
	{
		const FGMASBoundQueueV2RemoveEffectOperation Data = PayloadData.Get<FGMASBoundQueueV2RemoveEffectOperation>();
		RemoveEffectByIdSafe(Data.EffectIDs, EGMCAbilityEffectQueueType::Predicted);

		if (!HasAuthority() && !BoundQueueV2.bInBatchDispatch)
		{
			// Make an operation to confirm the effect application
			BoundQueueV2.OperationData  = FInstancedStruct::Make<FGMASBoundQueueV2AcknowledgeOperation>(FGMASBoundQueueV2AcknowledgeOperation{OperationID});
		}

		return true;
	}

	// Add Impulse
	if (StructType == FGMASBoundQueueV2AddImpulseOperation::StaticStruct())
	{
		const FGMASBoundQueueV2AddImpulseOperation KBData = PayloadData.Get<FGMASBoundQueueV2AddImpulseOperation>();
		GMCMovementComponent->AddImpulse(KBData.Impulse, KBData.bVelocityChange);
		if (!HasAuthority() && !BoundQueueV2.bInBatchDispatch)
		{
			// Make an operation to confirm the impulse application
			BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2AcknowledgeOperation>(FGMASBoundQueueV2AcknowledgeOperation{OperationID});
		}
		return true;
	}

	// Set Actor Location
	if (StructType == FGMASBoundQueueV2SetActorLocationOperation::StaticStruct())
	{
		const FGMASBoundQueueV2SetActorLocationOperation LocationData = PayloadData.Get<FGMASBoundQueueV2SetActorLocationOperation>();
		GetOwner()->SetActorLocation(LocationData.Location);
		if (!HasAuthority() && !BoundQueueV2.bInBatchDispatch)
		{
			// Make an operation to confirm the location change
			BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2AcknowledgeOperation>(FGMASBoundQueueV2AcknowledgeOperation{OperationID});
		}
		return true;
	}

	// No valid event found, so nothing to ack. Send a blank.
	if (!BoundQueueV2.bInBatchDispatch)
	{
		BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(FGMASBoundQueueV2OperationBaseData{});
	}
	return false;
}

void UGMC_AbilitySystemComponent::ProcessEffectApplicationFromOperation(const FGMASBoundQueueV2ApplyEffectOperation& Data)
{
	if (!Data.EffectClass) return;

	// Idempotency on replay. CL_OnRepAPMove → CL_ReplayMoves → ExecuteMove re-runs the same
	// move log on the autonomous proxy. Each replayed GenPredictionTick re-invokes
	// ProcessOperation on the same Apply op, which would otherwise call ApplyAbilityEffect
	// a second time. With the existing effect already in ActiveEffects under Data.EffectID,
	// the second call would either:
	//   - overwrite the TMap entry (Add semantics), orphaning the live instance, OR
	//   - take the IsValid() branch with EffectData.EffectID==0 → GetNextAvailableEffectID
	//     would skip 338 (taken) and return 339, creating a *second* live instance.
	// The latter is what the [ApplyTrace] logs caught: live id=338 then replayed id=339,
	// both ticking → bound attributes drain/regen at 2× rate on the client → forced
	// corrections → continuous chain replay.
	// The bound state has already been replayed so the existing instance carries the
	// correct snapshot — there is nothing for a second instantiation to add.
	if (ActiveEffects.Contains(Data.EffectID))
	{
		return;
	}

	UGMCAbilityEffect* Effect;
	int OutEffectHandle;
	int OutEffectId;

	if (Data.EffectData.IsValid())
	{
		// Slam the EffectID here too — the inline EffectData carries no ID by default
		// (callers fill Modifiers/Tags but rarely the nested EffectID). Without this,
		// ApplyAbilityEffect would fall through to GetNextAvailableEffectID and assign
		// a fresh ID instead of using the authoritative ID from the operation.
		FGMCAbilityEffectData InitData = Data.EffectData;
		InitData.EffectID = Data.EffectID;
		ApplyAbilityEffect(Data.EffectClass, InitData, EGMCAbilityEffectQueueType::Predicted, OutEffectHandle, OutEffectId, Effect);
	}
	else
	{
		// Apply the CDO's default effect data with the authoritative EffectID.
		FGMCAbilityEffectData DefaultData = Data.EffectClass->GetDefaultObject<UGMCAbilityEffect>()->EffectData;
		DefaultData.EffectID = Data.EffectID;
		ApplyAbilityEffect(Data.EffectClass, DefaultData, EGMCAbilityEffectQueueType::Predicted, OutEffectHandle, OutEffectId, Effect);
	}

	// Auto validate the effect since this was added via a server operation
	if (!HasAuthority() && Effect != nullptr)
	{
		ProcessedEffectIDs[Effect->EffectData.EffectID] = EGMCEffectAnswerState::Validated;
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("Applied Effect: %s"), *GetNameSafe(Data.EffectClass));
	}
}

void UGMC_AbilitySystemComponent::ServerProcessOperation(const FInstancedStruct& OperationData, bool bFromMovementTick)
{
	if (!IsAuthorityForGMASLogic()) return;
	if (!BoundQueueV2.IsValidGMASOperation(OperationData))
	{
		// Diagnostic: signal when an op arrived but the GMAS validation rejected it.
		// Empty/default ops (OperationID==0) are routine and not interesting; only log
		// when there is real payload data getting dropped.
		if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
		{
			const FGMASBoundQueueV2OperationBaseData* BD = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
			if (BD && BD->OperationID != 0)
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ServerOpDrop] op=%d reason=IsValidGMASOperation_failed struct=%s"),
					BD->OperationID,
					OperationData.GetScriptStruct() ? *OperationData.GetScriptStruct()->GetName() : TEXT("null"));
			}
		}
		return;
	}

	const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
	if (!BaseData)
	{
		// Defense-in-depth: IsValidGMASOperation above should already filter this,
		// but a future caller could bypass the validator. Drop silently and log.
		UE_LOG(LogGMCAbilitySystem, Error,
			TEXT("[ServerOpDrop] BaseData cast failed despite IsValidGMASOperation pass (struct=%s)"),
			OperationData.GetScriptStruct() ? *OperationData.GetScriptStruct()->GetName() : TEXT("null"));
		return;
	}
	const int OperationID = BaseData->OperationID;

	// Batched ack carries its ids in AcknowledgedIDs and intentionally leaves the base OperationID at
	// 0, so it MUST be handled before the OperationID==0 bail below. Otherwise every batched
	// server-auth acknowledgement is silently dropped at that guard, the grace map is never cleared,
	// and those effects only ever land via the grace-timeout forced drain (~1s late, reverse order).
	// This mirrors how ProcessOperation handles FGMASBoundQueueV2BatchOperation before its own
	// OperationID==0 guard. The single-ack path needs no such hoist: it carries a real OperationID.
	if (OperationData.GetScriptStruct() == FGMASBoundQueueV2BatchAcknowledgeOperation::StaticStruct())
	{
		if (BoundQueueV2.IsValidClientOperation(OperationData))
		{
			if (const FGMASBoundQueueV2BatchAcknowledgeOperation* BatchAck =
					OperationData.GetPtr<FGMASBoundQueueV2BatchAcknowledgeOperation>())
			{
				for (const int32 AckID : BatchAck->AcknowledgedIDs)
				{
					ServerProcessAcknowledgedOperation(AckID, bFromMovementTick);
				}
			}
		}
		return;
	}

	// Empty Operation, Ignore
	if (OperationID == 0) return;

	// Security check
	// These are operations the client has requested, ie Activate Ability
	if (BoundQueueV2.IsValidClientOperation(OperationData))
	{
		// A server-auth operation is ack'd, should have everything needed to go process it
		if (OperationData.GetScriptStruct() == FGMASBoundQueueV2AcknowledgeOperation::StaticStruct())
		{
			ServerProcessAcknowledgedOperation(BaseData->OperationID, bFromMovementTick);
			return;
		}

		// (Batched ack is handled earlier, before the OperationID==0 guard, since it carries
		//  OperationID==0 — see the hoisted block above.)

		if (!BoundQueueV2.HasPayloadByID(OperationID))
		{
			BoundQueueV2.CacheOperationPayload(OperationID, OperationData);
		}

		// Diagnostic: server actually received and accepted a client op. Pair this with the
		// client-side Apply trace to see if the op makes it across the wire at all.
		if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
		{
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[ServerOpAccept] op=%d struct=%s fromMove=%d"),
				OperationID,
				OperationData.GetScriptStruct() ? *OperationData.GetScriptStruct()->GetName() : TEXT("null"),
				bFromMovementTick ? 1 : 0);
		}

		// Client-auth effect application: the payload carries everything the server needs
		// (EffectClass, EffectID, EffectData). ProcessOperation does not have a branch for
		// this op type, so we handle it here, before handing off to the generic pipeline.
		if (OperationData.GetScriptStruct() == FGMASBoundQueueV2ClientAuthEffectOperation::StaticStruct())
		{
			const FGMASBoundQueueV2ClientAuthEffectOperation* Op =
				OperationData.GetPtr<FGMASBoundQueueV2ClientAuthEffectOperation>();
			if (!Op || !Op->EffectClass)
			{
				return;
			}

			// Whitelist gate — only effects that the server explicitly trusts may be applied.
			if (!IsClientAuthorizedEffect(Op->EffectClass))
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ServerProcessOperation] Rejected ClientAuth effect %s -- not whitelisted (potential cheat attempt)."),
					*Op->EffectClass->GetName());
				return;
			}

			// Anti-cheat: verify EffectID is in the client-auth reserved range.
			// Without this check, a malicious client could spoof an ID in the server-auth
			// range and overwrite an existing Predicted/ServerAuth effect server-side.
			if (Op->EffectID < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset)
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ServerProcessOperation] ClientAuth effect %s with non-client-auth ID range: %d "
					     "(potential cheat attempt -- refusing)."),
					*Op->EffectClass->GetName(), Op->EffectID);
				return;
			}

			// Anti-cheat + idempotency: the client-supplied EffectID must be UNIQUE.
			// ApplyAbilityEffect ends with ActiveEffects.Add(ID, Effect), which silently
			// OVERWRITES an existing entry — the old instance is orphaned (its modifiers
			// never rolled back) while the new one ticks. A malicious client replaying the
			// same ID could stack orphaned modifiers at will; a duplicate delivery of the
			// same op would corrupt state the same way by accident.
			if (ActiveEffects.Contains(Op->EffectID))
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ServerProcessOperation] ClientAuth effect %s rejected: EffectID %d already active "
					     "(duplicate delivery or potential cheat attempt)."),
					*Op->EffectClass->GetName(), Op->EffectID);
				UE_LOG(LogTemp, Warning,
					TEXT("[ServerProcessOperation] ClientAuth effect %s rejected: EffectID %d already active."),
					*Op->EffectClass->GetName(), Op->EffectID);
				return;
			}

			// Apply server-side using the client-supplied EffectID for cross-side coherence.
			FGMCAbilityEffectData InitData = Op->EffectData;
			InitData.EffectID = Op->EffectID;
			InitData.bServerAuth = false;
			InitData.bClientAuth = true;

			UGMCAbilityEffect* DuplicatedEffect = DuplicateObject<UGMCAbilityEffect>(
				Op->EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			UGMCAbilityEffect* AppliedEffect = ApplyAbilityEffect(DuplicatedEffect, InitData);
			if (AppliedEffect)
			{
				BoundActiveEffectIDs_Add(Op->EffectID);
			}
			return;
		}

		// Client-auth effect removal: the payload carries the EffectIDs the client wants
		// removed. Server validates every ID is in the client-auth reserved range before
		// applying, preventing a malicious client from removing standard-range effects.
		if (OperationData.GetScriptStruct() == FGMASBoundQueueV2ClientAuthRemoveEffectOperation::StaticStruct())
		{
			const FGMASBoundQueueV2ClientAuthRemoveEffectOperation* Op =
				OperationData.GetPtr<FGMASBoundQueueV2ClientAuthRemoveEffectOperation>();
			if (!Op || Op->EffectIDs.IsEmpty())
			{
				return;
			}

			// Anti-cheat: every ID must be in the client-auth range.
			for (const int Id : Op->EffectIDs)
			{
				if (Id < ClientAuthEffectIDOffset)
				{
					UE_LOG(LogGMCAbilitySystem, Warning,
						TEXT("[ServerProcessOperation] ClientAuth remove rejected: ID %d not in client-auth range "
						     "(potential cheat attempt)."), Id);
					return;
				}
			}

			// Apply removal locally on the server.
			for (const int Id : Op->EffectIDs)
			{
				if (ActiveEffects.Contains(Id))
				{
					RemoveActiveAbilityEffect(ActiveEffects[Id]);
				}
			}
			return;
		}

		// Client-auth ability activation: the payload carries everything the server needs
		// (AbilityClass, InputTag, InputAction). No RPCConfirmAbilityActivation is issued —
		// the client already trusts the activation by construction.
		if (OperationData.GetScriptStruct() == FGMASBoundQueueV2ClientAuthAbilityActivationOperation::StaticStruct())
		{
			const FGMASBoundQueueV2ClientAuthAbilityActivationOperation* Op =
				OperationData.GetPtr<FGMASBoundQueueV2ClientAuthAbilityActivationOperation>();
			if (!Op || !Op->AbilityClass)
			{
				return;
			}

			// Whitelist gate — only abilities the server explicitly trusts may be activated.
			if (!IsClientAuthorizedAbility(Op->AbilityClass))
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("[ServerProcessOperation] Rejected ClientAuth ability %s -- not whitelisted (potential cheat attempt)."),
					*Op->AbilityClass->GetName());
				return;
			}

			// Mirror client behavior: reduced gate set, skip standard CheckActivationTags.
			const UGMCAbility* CDO = Op->AbilityClass->GetDefaultObject<UGMCAbility>();
			if (!CheckActivationTagsForClientAuth(CDO))
			{
				// Client already saw the same block; drop silently.
				return;
			}

			// Operation-derived ID so the server instance pairs with the client's local
			// activation (heartbeats and task payloads address abilities by this ID).
			TryActivateAbility(Op->AbilityClass, Op->InputAction, Op->InputTag,
				/*bSkipActivationTagsCheck=*/true,
				DeriveAbilityIDFromOperation(Op->OperationID, 0));
			// No RPCConfirmAbilityActivation -- client trusts the activation by construction.
			return;
		}

		ProcessOperation(OperationData, bFromMovementTick);
	}
	else
	{
		// Diagnostic: op reached the server but failed the IsValidClientOperation security check.
		// This is the most likely failure point for "Sprint Not Confirmed By Server" — a client
		// op gets dropped here and never reaches ProcessOperation, so no apply happens server-side
		// and the client's Predicted timeout fires after 1s.
		if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
		{
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[ServerOpDrop] op=%d reason=IsValidClientOperation_failed struct=%s fromMove=%d"),
				OperationID,
				OperationData.GetScriptStruct() ? *OperationData.GetScriptStruct()->GetName() : TEXT("null"),
				bFromMovementTick ? 1 : 0);
		}
	}
}

void UGMC_AbilitySystemComponent::ServerProcessAcknowledgedOperation(int OperationID, bool bFromMovementTick)
{
	// Everything else should be server built operations that the client has confirmed
	// Ie, applied server-auth effects or server-auth events

	if (!BoundQueueV2.HasPayloadByID(OperationID) || !BoundQueueV2.ServerQueuedBoundOperationsGracePeriods.Contains(OperationID))
	{
		return;
	}


	FInstancedStruct PayloadData = BoundQueueV2.GetPayloadByID(OperationID);

	if (ProcessOperation(PayloadData, bFromMovementTick))
	{
		BoundQueueV2.ServerAcknowledgeOperation(OperationID);
	}
}


void UGMC_AbilitySystemComponent::AddImpulse(FVector Impulse, bool bVelChange)
{
	if (!HasAuthority())
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Client attempted to apply server-auth event"));
		return;
	}
	//
	FGMASBoundQueueV2AddImpulseOperation ImpulseOperation;
	ImpulseOperation.Impulse = Impulse;
	ImpulseOperation.bVelocityChange = bVelChange;
	const int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2AddImpulseOperation>(ImpulseOperation);
	BoundQueueV2.QueueServerOperation(OperationID);
}

void UGMC_AbilitySystemComponent::SetActorLocation(FVector Location)
{
	if (!HasAuthority())
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Client attempted to apply server-auth event"));
		return;
	}
	
	FGMASBoundQueueV2SetActorLocationOperation ImpulseOperation;
	ImpulseOperation.Location = Location;
	const int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2SetActorLocationOperation>(ImpulseOperation);
	BoundQueueV2.QueueServerOperation(OperationID);
}

void UGMC_AbilitySystemComponent::OnRep_UnBoundAttributes()
{
	if (OldUnBoundAttributes.Items.Num() != UnBoundAttributes.Items.Num())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("OnRep_UnBoundAttributes: Mismatched Attribute Old != New Value !"));
	}
}

void UGMC_AbilitySystemComponent::CheckUnBoundAttributeChanged()
{
	TArray<FAttribute>& OldAttributes = OldUnBoundAttributes.Items;
	const TArray<FAttribute>& CurrentAttributes = UnBoundAttributes.Items;

	// Two-phase by VALUE: collect changes and sync the old set FIRST, broadcast LAST.
	// The previous implementation held float* into OldAttributes across the delegate
	// broadcasts — any handler mutating the unbound attribute set (add/remove) reallocates
	// the TArray and every remaining pointer dangles. It also iterated the live
	// CurrentAttributes array while broadcasting, with the same re-entrancy hazard.
	struct FAttributeChange { FGameplayTag Tag; float OldValue; float NewValue; };
	TArray<FAttributeChange> Changes;

	for (const FAttribute& Attribute : CurrentAttributes)
	{
		for (FAttribute& OldAttribute : OldAttributes)
		{
			if (OldAttribute.Tag == Attribute.Tag)
			{
				if (OldAttribute.Value != Attribute.Value)
				{
					Changes.Add({ Attribute.Tag, OldAttribute.Value, Attribute.Value });
					OldAttribute.Value = Attribute.Value;
				}
				break;
			}
		}
	}

	for (const FAttributeChange& Change : Changes)
	{
		NativeAttributeChangeDelegate.Broadcast(Change.Tag, Change.OldValue, Change.NewValue);
		OnAttributeChanged.Broadcast(Change.Tag, Change.OldValue, Change.NewValue);
	}
}

int UGMC_AbilitySystemComponent::GetNextAvailableEffectID() const
{
	if (ActionTimer == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("[ApplyAbilityEffect] Action Timer is 0, cannot generate Effect ID. Is it a listen server smoothed pawn?"));
		return -1;
	}
		
	int NewEffectID = static_cast<int>(ActionTimer * 100);
	if (NewEffectID >= ServerAuthEffectIDOffset)
	{
		// Session has been running long enough that the standard (Predicted) ID range overlaps
		// with the server-auth reserved range. Fail-fast — silent overlap would let a predicted
		// id collide with a server-auth id and corrupt dispatch in ServerProcessOperation.
		UE_LOG(LogGMCAbilitySystem, Fatal,
			TEXT("[GetNextAvailableEffectID] ActionTimer overflow into server-auth range. "
			     "ActionTimer=%f -> ID=%d >= Offset=%d. Session too long?"),
			ActionTimer, NewEffectID, ServerAuthEffectIDOffset);
	}
	while (ActiveEffects.Contains(NewEffectID) || ReservedEffectIDs.Contains(NewEffectID))
	{
		NewEffectID++;
	}
	UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[Server: %hhd] Generated Effect ID: %d"), HasAuthority(), NewEffectID);

	return NewEffectID;
}

int UGMC_AbilitySystemComponent::GetNextAvailableServerAuthEffectID() const
{
	if (ActionTimer == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error,
			TEXT("[GetNextAvailableServerAuthEffectID] ActionTimer is 0, cannot generate Effect ID."));
		return -1;
	}

	int NewEffectID = static_cast<int>(ActionTimer * 100) + ServerAuthEffectIDOffset;
	if (NewEffectID >= ClientAuthEffectIDOffset)
	{
		// Server-auth range exhausted — would overflow up into the client-auth range. Same
		// fail-fast policy as the other helpers: a silent overlap corrupts the dispatcher logic.
		UE_LOG(LogGMCAbilitySystem, Fatal,
			TEXT("[GetNextAvailableServerAuthEffectID] EffectID overflow into client-auth range. "
			     "ActionTimer=%f -> ID=%d >= Offset=%d. Session too long?"),
			ActionTimer, NewEffectID, ClientAuthEffectIDOffset);
	}
	while (ActiveEffects.Contains(NewEffectID) || ReservedEffectIDs.Contains(NewEffectID))
	{
		NewEffectID++;
	}
	UE_LOG(LogGMCAbilitySystem, VeryVerbose,
		TEXT("[Server: %hhd] Generated ServerAuth Effect ID: %d"), HasAuthority(), NewEffectID);

	return NewEffectID;
}

int UGMC_AbilitySystemComponent::GetNextAvailableClientAuthEffectID() const
{
	if (ActionTimer == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error,
			TEXT("[GetNextAvailableClientAuthEffectID] ActionTimer is 0, cannot generate Effect ID."));
		return -1;
	}

	int NewEffectID = static_cast<int>(ActionTimer * 100) + ClientAuthEffectIDOffset;
	if (NewEffectID < ClientAuthEffectIDOffset)
	{
		// Wrap into negative (or back into the standard range) — session has been running
		// beyond the client-auth ID capacity. Same fail-fast policy as the standard helper:
		// silent overlap would corrupt the dispatcher logic.
		UE_LOG(LogGMCAbilitySystem, Fatal,
			TEXT("[GetNextAvailableClientAuthEffectID] EffectID overflow. "
			     "ActionTimer=%f -> ID=%d wrapped below Offset=%d."),
			ActionTimer, NewEffectID, ClientAuthEffectIDOffset);
	}
	while (ActiveEffects.Contains(NewEffectID) || ReservedEffectIDs.Contains(NewEffectID))
	{
		NewEffectID++;
	}
	UE_LOG(LogGMCAbilitySystem, VeryVerbose,
		TEXT("[Server: %hhd] Generated ClientAuth Effect ID: %d"), HasAuthority(), NewEffectID);
	return NewEffectID;
}

int32 UGMC_AbilitySystemComponent::GetNextAvailableEffectHandle() const
{
	if (ActionTimer == 0)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("[ApplyAbilityEffect] Action Timer is 0, cannot generate Effect ID. Is it a listen server smoothed pawn?"));
		return -1;
	}
		
	int NewEffectHandle = static_cast<int>(ActionTimer * 100);
	while (EffectHandles.Contains(NewEffectHandle))
	{
		NewEffectHandle++;
	}
	
	return NewEffectHandle;	
}

void UGMC_AbilitySystemComponent::GetEffectFromHandle_BP(int EffectHandle, bool& bOutSuccess, int32& OutEffectNetworkId,
                                                         UGMCAbilityEffect*& OutEffect)
{
	bOutSuccess = GetEffectFromHandle(EffectHandle, OutEffectNetworkId, OutEffect);
}

bool UGMC_AbilitySystemComponent::GetEffectFromHandle(int EffectHandle, int32& OutEffectNetworkId,
	UGMCAbilityEffect*& OutEffect) const
{
	FGMASQueueOperationHandle HandleData;

	if (!GetEffectHandle(EffectHandle, HandleData)) return false;

	OutEffectNetworkId = HandleData.NetworkId;
	if (HandleData.NetworkId > 0 && ActiveEffects.Contains(HandleData.NetworkId))
	{
		OutEffect = ActiveEffects[HandleData.NetworkId];
	}
	return true;
}

bool UGMC_AbilitySystemComponent::GetEffectHandle(int EffectHandle, FGMASQueueOperationHandle& HandleData) const
{
	for (auto& [ID, Handle] : EffectHandles)
	{
		if (Handle.Handle == EffectHandle)
		{
			HandleData = Handle;
			return true;
		}
	}
	return false;
}

void UGMC_AbilitySystemComponent::RemoveEffectHandle(int EffectHandle)
{
	EffectHandles.Remove(EffectHandle);
}

void UGMC_AbilitySystemComponent::ApplyAbilityEffectSafe(TSubclassOf<UGMCAbilityEffect> EffectClass,
                                                         FGMCAbilityEffectData InitializationData, EGMCAbilityEffectQueueType QueueType, bool& OutSuccess, int& OutEffectHandle, int& OutEffectId,
                                                         UGMCAbilityEffect*& OutEffect, UGMCAbility* HandlingAbility)
{
	// If no data is provided (no Modifiers / Tags / etc.), use the default data from the effect class.
	// IsValid() inspects the actual content fields; the previously-used operator== compared only StartTime/EndTime
	// (both default 0), which silently swapped runtime-supplied EffectData for the CDO whenever the caller didn't
	// set those timestamps — discarding inline Modifiers built at the call site.
	const FGMCAbilityEffectData EffectData = InitializationData.IsValid()
		? InitializationData
		: EffectClass->GetDefaultObject<UGMCAbilityEffect>()->EffectData;
	
	OutSuccess = ApplyAbilityEffect(EffectClass, EffectData, QueueType, OutEffectHandle, OutEffectId, OutEffect);
	
	if (OutSuccess && HandlingAbility)
	{
		HandlingAbility->DeclareEffect(OutEffectId, QueueType);
	}
}


UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffectShort(TSubclassOf<UGMCAbilityEffect> EffectClass,
                                                                        EGMCAbilityEffectQueueType QueueType, UGMCAbility* HandlingAbility)
{
	
	bool bOutSuccess;
	int OutEffectHandle;
	int OutEffectId;
	UGMCAbilityEffect* OutEffect = nullptr;
	
	ApplyAbilityEffectSafe(EffectClass, FGMCAbilityEffectData{}, QueueType, bOutSuccess, OutEffectHandle, OutEffectId, OutEffect, HandlingAbility);
	return bOutSuccess ? OutEffect : nullptr;
}


bool UGMC_AbilitySystemComponent::ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> EffectClass,
                                                     FGMCAbilityEffectData InitializationData,
                                                     EGMCAbilityEffectQueueType QueueType,
                                                     int& OutEffectHandle,
                                                     int& OutEffectId,
                                                     UGMCAbilityEffect*& OutEffect)
{
	OutEffect = nullptr;
	OutEffectId = -1;
	OutEffectHandle = -1;
	if (EffectClass == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to apply Effect, but effect is null!"));
		return false;
	}
	
	switch(QueueType)
	{
	case EGMCAbilityEffectQueueType::Predicted:
		{
			// Apply effect immediately.
			UGMCAbilityEffect* Effect = DuplicateObject(EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			OutEffect = ApplyAbilityEffect(Effect, InitializationData);
			// Inner Apply can return nullptr now (e.g. bUniqueByEffectTag rejection).
			// Out params already initialised to sentinels (-1 / nullptr) at function top;
			// signal failure to the caller via bSuccess=false rather than crashing.
			if (OutEffect == nullptr) { return false; }
			OutEffectId = OutEffect->EffectData.EffectID;
			// OutEffectHandle is a deprecated alias of OutEffectId — see RemoveEffectByHandle's
			// deprecation note. Mirror the id here so the legacy handle-based lookups keep
			// returning the same effect; new callers should use OutEffectId directly.
			OutEffectHandle = OutEffectId;
			return true;
		}
	case EGMCAbilityEffectQueueType::PredictedQueued:
		{
			if (GMCMovementComponent->IsExecutingMove() || bInAncillaryTick)
			{
				// Inside a movement tick — apply immediately (same as Predicted).
				UGMCAbilityEffect* Effect = DuplicateObject(EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
				OutEffect = ApplyAbilityEffect(Effect, InitializationData);
				if (OutEffect)
				{
					OutEffectId = OutEffect->EffectData.EffectID;
					OutEffectHandle = OutEffectId;
				}
			}
			else
			{
				// Outside movement tick — buffer for processing at the next tick.
				FGMASBoundQueueV2ApplyEffectOperation ApplyOp;
				ApplyOp.EffectClass = EffectClass;
				ApplyOp.EffectData = InitializationData;
				PendingPredictedOperations.Add(FInstancedStruct::Make(ApplyOp));
			}
			return true;
		}
	case EGMCAbilityEffectQueueType::ServerAuthMove:
	case EGMCAbilityEffectQueueType::ServerAuth:
		{
			// Client does not apply effects in these queues, only the server does
			if (!HasAuthority())
			{
				return false;
			}


			FGMASBoundQueueV2ApplyEffectOperation EffectActivationData;
			EffectActivationData.EffectClass = EffectClass;
			EffectActivationData.EffectData = InitializationData;
			EffectActivationData.EffectID = GetNextAvailableServerAuthEffectID();
			ReservedEffectIDs.Add(EffectActivationData.EffectID);

			// We return back the Operation ID instead of the Effect ID which isn't great
			OutEffectId =EffectActivationData.EffectID;
			int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(EffectActivationData);
			OutEffectHandle = OutEffectId;
			BoundQueueV2.QueueServerOperation(OperationID);
			return true;
		}

	case EGMCAbilityEffectQueueType::ServerInstantAttribute:
		{
			// Server-only fast path. Apply in the current tick without queuing through BoundQueueV2.
			// Attribute modifiers reach clients via the standard FAttribute bound binding.
			// Misuse on a non-qualifying effect would corrupt the bound state, so guards below
			// fall back to ServerAuth rather than proceeding silently.
			if (!HasAuthority())
			{
				UE_LOG(LogGMCAbilitySystem, Error,
					TEXT("ServerInstantAttribute apply rejected: %s called on non-authority. ServerInstantAttribute is server-only."),
					*EffectClass->GetName());
				return false;
			}

			const UGMCAbilityEffect* CDO = EffectClass->GetDefaultObject<UGMCAbilityEffect>();
			const FGMCAbilityEffectData& CDOData = CDO->EffectData;

			const bool bIsInstant         = CDOData.EffectType == EGMASEffectType::Instant;
			const bool bHasGrantedTags    = !CDOData.GrantedTags.IsEmpty() || !InitializationData.GrantedTags.IsEmpty();
			const bool bHasGrantedAbil    = !CDOData.GrantedAbilities.IsEmpty() || !InitializationData.GrantedAbilities.IsEmpty();

			if (!bIsInstant || bHasGrantedTags || bHasGrantedAbil)
			{
				ensureMsgf(bIsInstant,
					TEXT("ServerInstantAttribute expects EffectType::Instant on %s — non-Instant effects need the GMC bound queue to tick correctly. Falling back to ServerAuth."),
					*EffectClass->GetName());
				ensureMsgf(!bHasGrantedTags,
					TEXT("ServerInstantAttribute cannot grant tags on %s — GrantedTags route through the bound ActiveTags container and must use ServerAuth. Falling back to ServerAuth."),
					*EffectClass->GetName());
				ensureMsgf(!bHasGrantedAbil,
					TEXT("ServerInstantAttribute cannot grant abilities on %s — relies on the bound ability map. Falling back to ServerAuth."),
					*EffectClass->GetName());

				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("ServerInstantAttribute guards failed for %s (Instant=%d GrantedTags=%d GrantedAbil=%d) — falling back to ServerAuth."),
					*EffectClass->GetName(), bIsInstant ? 1 : 0, bHasGrantedTags ? 1 : 0, bHasGrantedAbil ? 1 : 0);

				return ApplyAbilityEffect(EffectClass, InitializationData,
					EGMCAbilityEffectQueueType::ServerAuth, OutEffectHandle, OutEffectId, OutEffect);
			}

			UGMCAbilityEffect* Effect = DuplicateObject(CDO, this);
			OutEffect = ApplyAbilityEffect(Effect, InitializationData);
			if (OutEffect == nullptr) { return false; }
			OutEffectId = OutEffect->EffectData.EffectID;
			OutEffectHandle = OutEffectId;
			return true;
		}

	case EGMCAbilityEffectQueueType::ClientAuth:
		{
			if (!IsClientAuthorizedEffect(EffectClass))
			{
				UE_LOG(LogGMCAbilitySystem, Warning,
					TEXT("ClientAuth apply rejected: %s not in ClientAuthorizedAbilityEffects."),
					EffectClass ? *EffectClass->GetName() : TEXT("(null)"));
				return false;
			}

			// Allocate ID in the reserved high range BEFORE calling the inner Apply
			// so the inner generator does not pick a standard-range ID.
			const int ClientAuthEffectID = GetNextAvailableClientAuthEffectID();
			if (ClientAuthEffectID == -1)
			{
				return false;  // ActionTimer == 0, error already logged
			}

			UGMCAbilityEffect* DuplicatedEffect = DuplicateObject<UGMCAbilityEffect>(
				EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			if (!DuplicatedEffect)
			{
				return false;
			}

			InitializationData.bServerAuth = false;
			InitializationData.bClientAuth = true;
			InitializationData.EffectID = ClientAuthEffectID;
			OutEffect = ApplyAbilityEffect(DuplicatedEffect, InitializationData);
			if (!OutEffect)
			{
				return false;
			}
			OutEffectId = OutEffect->EffectData.EffectID;
			OutEffectHandle = OutEffectId;

			if (!HasAuthority())
			{
				// Owning client side: pre-validate the effect locally — the server will
				// mirror the apply via the bound queue, no RPC confirmation expected.
				ProcessedEffectIDs.Add(OutEffectId, EGMCEffectAnswerState::Validated);

				FGMASBoundQueueV2ClientAuthEffectOperation Op;
				Op.EffectClass = EffectClass;
				Op.EffectID = OutEffectId;
				Op.EffectData = InitializationData;
				const int OpID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ClientAuthEffectOperation>(Op);
				BoundQueueV2.QueueClientOperation(OpID);
			}
			else
			{
				// Server-owned pawn (e.g. AI): no client→server payload to send.
				BoundActiveEffectIDs_Add(OutEffectId);
			}
			return true;
		}

	}

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("[%20s] %s attempted to apply effect of type %s but something has gone BADLY wrong!"),
		*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), *EffectClass->GetName())
	return false;
}

UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffectViaOperation(const FGMASBoundQueueV2ApplyEffectOperation& Operation)
{
	FGMCAbilityEffectData EffectData = {};
	if (Operation.EffectData.IsValid())
	{
		EffectData = Operation.EffectData;
	}
	else
	{
		EffectData = Operation.EffectClass->GetDefaultObject<UGMCAbilityEffect>()->EffectData;
	}
	
	
	UGMCAbilityEffect* Effect = DuplicateObject(Operation.EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
	Effect = ApplyAbilityEffect(Effect, EffectData);
	FString isExecutingMove = GMCMovementComponent->IsExecutingMove() ? TEXT("True") : TEXT("False");
	UE_LOG(LogTemp, Warning, TEXT("Applied Effect With Action Timer: %f | IsPredTick: %s | IsServer: %s"),
		ActionTimer, *isExecutingMove, HasAuthority() ? TEXT("True") : TEXT("False"));
	
	return Effect;
}

UGMCAbilityEffect* UGMC_AbilitySystemComponent::GetEffectById(const int EffectId) const
{
	if (!ActiveEffects.Contains(EffectId)) return nullptr;

	return ActiveEffects[EffectId];
}


TArray<UGMCAbilityEffect*> UGMC_AbilitySystemComponent::GetEffectsByIds(const TArray<int> EffectIds) const {
	TArray<UGMCAbilityEffect*> EffectsFound;

	for (int EffectId : EffectIds)
	{
		if (ActiveEffects.Contains(EffectId))
		{
			EffectsFound.Add(ActiveEffects[EffectId]);
		}
	}

	return EffectsFound;
}


FString UGMC_AbilitySystemComponent::GetEffectsNameAsString(const TArray<UGMCAbilityEffect*>& EffectList) const {
	FString EffectNames;
	for (UGMCAbilityEffect* Effect : EffectList)
	{
		EffectNames += (Effect ? GetNameSafe(Effect->GetClass()) : "NULLPTR EFFECT") + ", ";
	}
	return EffectNames;
}


UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData)
{
	if (Effect == nullptr) {
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMC_AbilitySystemComponent::ApplyAbilityEffect: effect is null!"));
		return nullptr;
	}

	// bUniqueByEffectTag: reject if a same-tag effect is functionally active; collect
	// deferred (EndAtActionTimer >= 0) ones to replace after the new is added below.
	// Done before InitializeEffect so a rejection has zero side effects.
	TArray<UGMCAbilityEffect*> DeferredMatchesToReplace;
	if (InitializationData.bUniqueByEffectTag && InitializationData.EffectTag.IsValid())
	{
		for (const TPair<int, UGMCAbilityEffect*>& Existing : ActiveEffects)
		{
			if (!Existing.Value
				|| Existing.Value == Effect
				|| !Existing.Value->EffectData.EffectTag.IsValid()
				|| !Existing.Value->EffectData.EffectTag.MatchesTagExact(InitializationData.EffectTag))
			{
				continue;
			}

			if (Existing.Value->EndAtActionTimer >= 0.0)
			{
				DeferredMatchesToReplace.Add(Existing.Value);
			}
			else
			{
				UE_LOG(LogGMCAbilitySystem, Verbose,
					TEXT("ApplyAbilityEffect rejected: bUniqueByEffectTag=true, tag '%s' already active on %s (id=%d)"),
					*InitializationData.EffectTag.ToString(),
					*GetNameSafe(GetOwner()),
					Existing.Value->EffectData.EffectID);
				return nullptr;
			}
		}
	}

	// Force the component this is being applied to to be the owner
	InitializationData.OwnerAbilityComponent = this;
	InitializationData.SourceAbilityComponent = this;

	Effect->InitializeEffect(InitializationData);
	
	if (Effect->EffectData.EffectID == 0)
	{
		Effect->EffectData.EffectID = GetNextAvailableEffectID();
	}

	if (HasAuthority())
	{
		// If this was a server-auth, the ID is already generated and needs to be cleaned up from reserved
		ReservedEffectIDs.Remove(Effect->EffectData.EffectID);
	}
	else
	{
		ProcessedEffectIDs.Add(Effect->EffectData.EffectID, EGMCEffectAnswerState::Pending);
	}

	ActiveEffects.Add(Effect->EffectData.EffectID, Effect);

	// Both sides write deterministically; server's value overwrites client on replication.
	BoundActiveEffectIDs_Add(Effect->EffectData.EffectID);

	// Replace deferred same-tag matches. Server force-ends immediately (authoritative);
	// client suspends the OLD pending the successor's Validated/Timeout verdict so a
	// server-rejected predict can revive the OLD instead of leaving it permanently dead.
	// Order: ActiveEffects.Add above must precede any EndEffect here so the multi-instance
	// preserve check counts the NEW as a sibling (no one-tick GrantedTags flicker).
	if (HasAuthority())
	{
		for (UGMCAbilityEffect* DeferredOld : DeferredMatchesToReplace)
		{
			if (!DeferredOld || DeferredOld->bCompleted)
			{
				continue;
			}
			DeferredOld->EndAtActionTimer = -1.0;
			DeferredOld->EndEffect();
		}
	}
	else
	{
		TArray<TWeakObjectPtr<UGMCAbilityEffect>> SuspendedOlds;
		SuspendedOlds.Reserve(DeferredMatchesToReplace.Num());
		for (UGMCAbilityEffect* DeferredOld : DeferredMatchesToReplace)
		{
			if (!DeferredOld || DeferredOld->bCompleted)
			{
				continue;
			}
			DeferredOld->bPendingDeathBySuccessor = true;
			SuspendedOlds.Add(DeferredOld);
		}
		if (SuspendedOlds.Num() > 0)
		{
			PendingReplacements.Add(Effect->EffectData.EffectID, MoveTemp(SuspendedOlds));
		}
	}

	// CVar-gated apply-path stackdump for chasing duplicate-apply bugs.
	if (GMASApplyTrace::CVarLogApplyTrace.GetValueOnGameThread())
	{
		const FString Filter = GMASApplyTrace::CVarApplyTraceFilter.GetValueOnGameThread();
		const FString ClassName = Effect->GetClass()->GetName();
		if (Filter.IsEmpty() || ClassName.Contains(Filter))
		{
			ANSICHAR CppStack[8192] = { 0 };
			FPlatformStackWalk::StackWalkAndDump(CppStack, sizeof(CppStack), /*IgnoreCount=*/1);
			const FString ScriptStack = FFrame::GetScriptCallstack(true);
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("[ApplyTrace] class=%s id=%d auth=%d action_t=%f netmode=%d\nC++ stack:\n%hs\nScript stack:\n%s"),
				*ClassName, Effect->EffectData.EffectID, HasAuthority() ? 1 : 0,
				ActionTimer, static_cast<int32>(GetNetMode()),
				CppStack, *ScriptStack);
		}
	}

	return Effect;
}

void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect)
{
	if (Effect == nullptr || !ActiveEffects.Contains(Effect->EffectData.EffectID)) return;

	// Anti-drift defer: for effects that keep ticking attributes after Remove is called, the side that ends the
	// effect later accumulates extra modifier applications. Affects both EffectTypes that drain over time:
	//   - Ticking : continuous drain proportional to DeltaTime (e.g. Stamina via SprintCost)
	//   - Periodic: discrete chunks fired at period boundaries (e.g. Recovery +X Stamina/s)
	// We defer EndEffect() on both client and server for the same logical move tick window (ClientGraceTime),
	// so each side fires the same number of Tick / period boundary applications before the effect actually ends.
	const bool bIsNetworked    = GetNetMode() != NM_Standalone;
	const bool bIsTimeDriven   = Effect->EffectData.EffectType == EGMASEffectType::Ticking
	                          || Effect->EffectData.EffectType == EGMASEffectType::Periodic;

	// Effective grace = per-effect override if explicitly set (>0), else server-wise project default
	// from UGMASNetworkTimingSettings. Lets designers tune a slow drain effect per-instance while
	// keeping the global RTT baseline configurable in Project Settings.
	const float EffectiveGraceTime = Effect->EffectData.ClientGraceTime > 0.f
		? Effect->EffectData.ClientGraceTime
		: GetDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime;
	// TEMP TEST (2026-05-22): grace-time removal defer DISABLED to observe Ticking/Periodic disappear instantly.
	// RESTORE with: const bool bHasGracePeriod = EffectiveGraceTime > 0.f;
	// WARNING: while off, a Predicted Remove of a time-driven effect can drift client vs server by ~RTT
	//          (different tick counts before EndEffect) → "was not valid" chain-replay on bound attributes (Bug #3).
	const bool bHasGracePeriod = false;

	if (bIsNetworked && bIsTimeDriven && bHasGracePeriod && !Effect->bCompleted)
	{
		// Idempotent arming on absolute ActionTimer. The first call latches EndAtActionTimer using
		// the current ActionTimer (which is identical on client and server replay because both
		// process this Remove at the same logical move tick — GMC bound state invariant). Re-arming
		// during the defer window must be a NO-OP: a duplicate Remove path (e.g. RPCClientEndEffect
		// landing on top of a local-replayed Remove) would otherwise reset the latch and shift the
		// end timestamp, breaking bilateral symmetry.
		if (Effect->EndAtActionTimer < 0.0)
		{
			Effect->EndAtActionTimer = ActionTimer + EffectiveGraceTime;
		}
		return;
	}

	Effect->EndEffect();
}

void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType)
{
	UGMCAbilityEffect* Effect =	GetEffectById(EffectHandle);
	if (Effect == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("[%20s] %s tried to remove effect with handle %d, but it doesn't exist!"),
			*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), EffectHandle);
		return;
	}

	RemoveActiveAbilityEffectSafe(Effect, QueueType);
}

void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffectSafe(UGMCAbilityEffect* Effect,
                                                                EGMCAbilityEffectQueueType QueueType)
{
	if (Effect == nullptr)
	{
		ensureAlwaysMsgf(false, TEXT("[%20s] %s tried to remove a null effect!"),
			*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName());
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("[%20s] %s tried to remove a null effect!"),
			*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName());
		return;
	}
		
	
	RemoveEffectByIdSafe({ Effect->EffectData.EffectID }, QueueType);
}


void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffectByTag(const FGameplayTag& Tag, EGMCAbilityEffectQueueType QueueType, bool bAllInstance) {

	if (!Tag.IsValid()) return;

	// Snapshot matching IDs before removing: RemoveEffectByIdSafe can run EndEffect
	// synchronously, and an OnEffectEnded handler that applies a new effect re-enters
	// ActiveEffects.Add mid-iteration — mutating the TMap invalidates this loop's iterator.
	// Same snapshot pattern as RemoveEffectByTagSafe / EffectsMatchingTag.
	TArray<int> MatchingIds;
	for (const auto& [EffectId, Effect] : ActiveEffects)
	{
		if (IsValid(Effect) && Effect->EffectData.EffectTag.MatchesTag(Tag))
		{
			MatchingIds.Add(EffectId);
			if (!bAllInstance) {
				break;
			}
		}
	}

	if (MatchingIds.Num() > 0)
	{
		RemoveEffectByIdSafe(MatchingIds, QueueType);
	}
}

TArray<int> UGMC_AbilitySystemComponent::EffectsMatchingTag(const FGameplayTag& Tag, int32 NumToRemove) const
{
	if (NumToRemove < -1 || !Tag.IsValid()) {
		return {};
	}

	TArray<int> EffectsToRemove;
	int NumRemoved = 0;
	
	for(const TTuple<int, UGMCAbilityEffect*> Effect : ActiveEffects)
	{
		if(NumRemoved == NumToRemove){
			break;
		}
		
		if(Effect.Value->EffectData.EffectTag.IsValid() && Effect.Value->EffectData.EffectTag.MatchesTagExact(Tag)){
			EffectsToRemove.Add(Effect.Value->EffectData.EffectID);
			NumRemoved++;
		}
	}

	return EffectsToRemove;	
}

int32 UGMC_AbilitySystemComponent::RemoveEffectByTag(FGameplayTag InEffectTag, int32 NumToRemove, bool bOuterActivation) {
	
	if (NumToRemove < -1 || !InEffectTag.IsValid()) {
		return 0;
	}

	TArray<int> EffectsToRemove = EffectsMatchingTag(InEffectTag, NumToRemove);

	if (EffectsToRemove.Num() > 0)
	{
		RemoveEffectById(EffectsToRemove, bOuterActivation);
	}

	return EffectsToRemove.Num();
}

int32 UGMC_AbilitySystemComponent::RemoveEffectByTagSafe(FGameplayTag InEffectTag, int32 NumToRemove,
	EGMCAbilityEffectQueueType QueueType)
{
	if (NumToRemove < -1 || !InEffectTag.IsValid()) {
		return 0;
	}

	TArray<int> EffectsToRemove = EffectsMatchingTag(InEffectTag, NumToRemove);
	

	if (EffectsToRemove.Num() > 0)
	{
		RemoveEffectByIdSafe(EffectsToRemove, QueueType);
	}

	return EffectsToRemove.Num();	
}

int UGMC_AbilitySystemComponent::RemoveEffectsByQuery(const FGameplayTagQuery& Query, EGMCAbilityEffectQueueType QueueType)
{
	int EffectsRemoved = 0;

	for (const auto& EffectEntry : ActiveEffects)
	{
		if (UGMCAbilityEffect* Effect = EffectEntry.Value)
		{
			if (Query.Matches(Effect->EffectData.EffectDefinition))
			{
				RemoveActiveAbilityEffectSafe(Effect, QueueType);
				EffectsRemoved++;
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Removed effect %s by query"), *Effect->EffectData.EffectTag.ToString());
			}
		}
	}
	return EffectsRemoved;
}

bool UGMC_AbilitySystemComponent::RemoveEffectByIdSafe(TArray<int> Ids, EGMCAbilityEffectQueueType QueueType)
{
	if (!Ids.Num()) {
		return true;
	}
	
	switch(QueueType) {
		case EGMCAbilityEffectQueueType::Predicted:
			{
				if (!GMCMovementComponent->IsExecutingMove() && GetNetMode() != NM_Standalone && !bInAncillaryTick)
				{

					ensureMsgf(false, TEXT("[%20s] %s attempted a predicted removal of effects outside of a movement cycle! (%s)"),
						*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), *GetEffectsNameAsString(GetEffectsByIds(Ids)));
					UE_LOG(LogGMCAbilitySystem, Error, TEXT("[%20s] %s attempted a predicted removal of effects outside of a movement cycle! (%s)"),
						*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), *GetEffectsNameAsString(GetEffectsByIds(Ids)));
					return false;
				}

				
				for (const int Id : Ids)
				{
					if (ActiveEffects.Contains(Id)) RemoveActiveAbilityEffect(ActiveEffects[Id]);
				}

				return true;
			}
		case EGMCAbilityEffectQueueType::PredictedQueued:
			{
				if (GMCMovementComponent->IsExecutingMove() || bInAncillaryTick)
				{
					// Inside a movement tick — remove immediately.
					TArray<UGMCAbilityEffect*> EffectsToRemove;
					for (const int Id : Ids)
					{
						if (ActiveEffects.Contains(Id))
						{
							EffectsToRemove.Add(ActiveEffects[Id]);
						}
					}
					for (UGMCAbilityEffect* Effect : EffectsToRemove)
					{
						RemoveActiveAbilityEffect(Effect);
					}
				}
				else
				{
					// Outside movement tick — buffer for processing at the next tick.
					FGMASBoundQueueV2RemoveEffectOperation RemoveOp;
					RemoveOp.EffectIDs = Ids;
					PendingPredictedOperations.Add(FInstancedStruct::Make(RemoveOp));
				}
				return true;
			}
		case EGMCAbilityEffectQueueType::ClientAuth:
		{
			// Anti-cheat: every ID must be in the client-auth reserved range.
			// The client cannot request removal of standard-range (Predicted/ServerAuth) effects.
			for (const int Id : Ids)
			{
				if (Id < ClientAuthEffectIDOffset)
				{
					UE_LOG(LogGMCAbilitySystem, Warning,
						TEXT("[RemoveEffectByIdSafe] ClientAuth remove rejected: ID %d not in client-auth range."), Id);
					return false;
				}
			}

			// Local removal first.
			for (const int Id : Ids)
			{
				if (ActiveEffects.Contains(Id))
				{
					RemoveActiveAbilityEffect(ActiveEffects[Id]);
				}
			}

			// Forward to the server (skip if we ARE the server).
			if (!HasAuthority())
			{
				FGMASBoundQueueV2ClientAuthRemoveEffectOperation Op;
				Op.EffectIDs = Ids;
				const int OpID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ClientAuthRemoveEffectOperation>(Op);
				BoundQueueV2.QueueClientOperation(OpID);
			}
			return true;
		}

		case EGMCAbilityEffectQueueType::ServerAuthMove:
		case EGMCAbilityEffectQueueType::ServerAuth:
			{
				if (!HasAuthority())
				{
					return false;
				}

				FGMASBoundQueueV2RemoveEffectOperation EffectRemovalData;
				EffectRemovalData.EffectIDs = Ids;
				const int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2RemoveEffectOperation>(EffectRemovalData);
				BoundQueueV2.QueueServerOperation(OperationID);
				return true;
			}

		case EGMCAbilityEffectQueueType::ServerInstantAttribute:
			{
				// Symmetric to the ServerInstantAttribute apply path: remove immediately on the server, no queue.
				// Reached only if a caller explicitly requests it; the typical ServerInstantAttribute apply target
				// is EffectType::Instant which self-ends on the first Tick, so an external Remove is
				// usually unnecessary. Kept for defense in depth.
				if (!HasAuthority())
				{
					UE_LOG(LogGMCAbilitySystem, Error,
						TEXT("ServerInstantAttribute remove rejected on non-authority."));
					return false;
				}

				for (const int Id : Ids)
				{
					if (ActiveEffects.Contains(Id))
					{
						RemoveActiveAbilityEffect(ActiveEffects[Id]);
					}
				}
				return true;
			}
	}

	ensureMsgf(false, TEXT("[%20s] %s attempted a removal of effects but something went horribly wrong! (%s)"),
		*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), *GetEffectsNameAsString(GetEffectsByIds(Ids)));
	UE_LOG(LogGMCAbilitySystem, Error, TEXT("[%20s] %s attempted a removal of effects but something went horribly wrong! (%s)"),
		*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(),*GetEffectsNameAsString(GetEffectsByIds(Ids)))
	return false;
}

bool UGMC_AbilitySystemComponent::RemoveEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType)
{
	// EffectHandle is now an alias of EffectId — see the deprecation note on the declaration.
	// We bypass the legacy GetEffectFromHandle path, which scans the half-implemented
	// EffectHandles registry (nothing populates it from the apply paths), and forward
	// straight to the id-based remove. Existing callers thus keep working with a warning,
	// rather than silently no-oping forever.
	if (EffectHandle <= 0)
	{
		return false;
	}
	return RemoveEffectByIdSafe({ EffectHandle }, QueueType);
}


bool UGMC_AbilitySystemComponent::RemoveEffectById(TArray<int> Ids, bool bOuterActivation) {

	// Just hit up the newer version.
	return RemoveEffectByIdSafe(Ids, bOuterActivation ? EGMCAbilityEffectQueueType::ServerAuth : EGMCAbilityEffectQueueType::Predicted);

}


int32 UGMC_AbilitySystemComponent::GetNumEffectByTag(FGameplayTag InEffectTag){
	if(!InEffectTag.IsValid()) return -1;
	int32 Count = 0;
	for (const TTuple<int, UGMCAbilityEffect*> Effect : ActiveEffects){
		if(Effect.Value->EffectData.EffectTag.IsValid() && Effect.Value->EffectData.EffectTag.MatchesTagExact(InEffectTag)){
			Count++;
		}
	}
	return Count;
}


TArray<const FAttribute*> UGMC_AbilitySystemComponent::GetAllAttributes() const{
	TArray<const FAttribute*> AllAttributes;

	for (int32 i = 0; i < UnBoundAttributes.Items.Num(); i++){
		AllAttributes.Add(&UnBoundAttributes.Items[i]);
	}

	for (int32 i = 0; i < BoundAttributes.Attributes.Num(); i++){
		AllAttributes.Add(&BoundAttributes.Attributes[i]);
	}

	return AllAttributes;
}


bool UGMC_AbilitySystemComponent::IsClientAuthorizedAbility(TSubclassOf<UGMCAbility> AbilityClass) const
{
	if (!AbilityClass)
	{
		return false;
	}
	return ClientAuthorizedAbilities.Contains(AbilityClass);
}

bool UGMC_AbilitySystemComponent::IsClientAuthorizedEffect(TSubclassOf<UGMCAbilityEffect> EffectClass) const
{
	if (!EffectClass)
	{
		return false;
	}
	return ClientAuthorizedAbilityEffects.Contains(EffectClass);
}

const FAttribute* UGMC_AbilitySystemComponent::GetAttributeByTag(FGameplayTag AttributeTag) const
{
	if (!AttributeTag.IsValid()) return nullptr;
	
	for (const FAttribute& Attribute : UnBoundAttributes.Items)
	{
		if (Attribute.Tag.MatchesTagExact(AttributeTag))
		{
			return &Attribute;
		}
	}

	for (const FAttribute& Attribute : BoundAttributes.Attributes)
	{
		if (Attribute.Tag.MatchesTagExact(AttributeTag))
		{
			return &Attribute;
		}
	}

	return nullptr;
}

float UGMC_AbilitySystemComponent::GetAttributeValueByTag(const FGameplayTag AttributeTag) const
{
	if (const FAttribute* Att = GetAttributeByTag(AttributeTag))
	{
		return Att->Value;
	}
	return 0.f;
}

float UGMC_AbilitySystemComponent::GetAttributeRawValue(FGameplayTag AttributeTag) const
{
	if (const FAttribute* Att = GetAttributeByTag(AttributeTag))
	{
		return Att->RawValue;
	}
	return 0.f;
}


FAttributeClamp UGMC_AbilitySystemComponent::GetAttributeClampByTag(FGameplayTag AttributeTag) const {
	if (const FAttribute* Att = GetAttributeByTag(AttributeTag))
	{
		return Att->Clamp;
	}
	return FAttributeClamp();
}


bool UGMC_AbilitySystemComponent::SetAttributeValueByTag(FGameplayTag AttributeTag, float NewValue, bool bResetModifiers)
{
	if (const FAttribute* Att = GetAttributeByTag(AttributeTag))
	{
		/*Att->SetBaseValue(NewValue);

		if (bResetModifiers)
		{
			Att->ResetModifiers();
		}

		Att->CalculateValue();*/
		UnBoundAttributes.MarkAttributeDirty(*Att);
		return true;
	}
	return false;
}

float UGMC_AbilitySystemComponent::GetAttributeInitialValueByTag(FGameplayTag AttributeTag) const{
	if(!AttributeTag.IsValid()){return -1.0f;}
	for(UGMCAttributesData* DataAsset : AttributeDataAssets){
		for(FAttributeData DefaultAttribute : DataAsset->AttributeData){
			if(DefaultAttribute.AttributeTag.IsValid() && AttributeTag.MatchesTagExact(DefaultAttribute.AttributeTag)){
				return DefaultAttribute.DefaultValue;
			}
		}
	}
	return -1.0f;
}

#pragma region ToStringHelpers

FString UGMC_AbilitySystemComponent::GetAllAttributesString() const{
	FString FinalString = TEXT("\n");
	for (const FAttribute* Attribute : GetAllAttributes()){
		FinalString += "[" + FString::FromInt(Attribute->BoundIndex) + "] " + Attribute->ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsDataString() const{
	// FString FinalString = FString::Printf(TEXT("%d total\n"), ActiveEffectsData.Num());
	// for(const FGMCAbilityEffectData& ActiveEffectData : ActiveEffectsData){
	// 	FinalString += ActiveEffectData.ToString() + TEXT("\n");
	// }
	// return FinalString;
	return "UNDER CONSTRUCTION";
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsString() const{
	FString FinalString = FString::Printf(TEXT("%d total\n"), ActiveEffects.Num());
	for(const TTuple<int, UGMCAbilityEffect*> ActiveEffect : ActiveEffects){
		FinalString += ActiveEffect.Value->ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveAbilitiesString() const{
	FString FinalString = TEXT("\n");
	for(const TTuple<int, UGMCAbility*> ActiveAbility : ActiveAbilities){
		FinalString += FString::Printf(TEXT("%d: "), ActiveAbility.Key) + ActiveAbility.Value->ToString() + TEXT("\n");
	}
	return FinalString;
}

#pragma endregion  ToStringHelpers


void UGMC_AbilitySystemComponent::ApplyAbilityAttributeModifier(const FGMCAttributeModifier& AttributeModifier)
{
	// Todo : re-add later
	// Broadcast the event to allow modifications to happen before application
	//OnPreAttributeChanged.Broadcast(AttributeModifierContainer, SourceAbilityComponent);
	
	if (const FAttribute* AffectedAttribute = GetAttributeByTag(AttributeModifier.AttributeTag))
	{
		// If attribute is unbound and this is the client that means we shouldn't predict.
		if(!AffectedAttribute->bIsGMCBound && !HasAuthority()) {
			return;
		}
		
		AffectedAttribute->AddModifier(AttributeModifier);
	}
}

//////////////// FX

UNiagaraComponent* UGMC_AbilitySystemComponent::SpawnParticleSystemAttached(FFXSystemSpawnParameters SpawnParams, 
	bool bIsClientPredicted, bool bDelayByGMCSmoothing)
{
	if (SpawnParams.SystemTemplate == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to spawn FX, but FX is null!"));
		return nullptr;
	}
	
	if (SpawnParams.WorldContextObject == nullptr)
	{
		SpawnParams.WorldContextObject = GetWorld();
	}

	if (HasAuthority())
	{
		MC_SpawnParticleSystemAttached(SpawnParams, bIsClientPredicted, bDelayByGMCSmoothing);
	}

	// Sim Proxies can delay FX by the smoothing delay to better line up
	if (bDelayByGMCSmoothing && !HasAuthority() && !IsLocallyControlledPawnASC())
	{
		float Delay = GMCMovementComponent->GetTime() - GMCMovementComponent->GetSmoothingTime();
		FTimerHandle DelayHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayHandle, [this, SpawnParams]()
		{
			UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(SpawnParams);
		}, Delay, false);
		return nullptr;
	}

	UNiagaraComponent* SpawnedComponent = UNiagaraFunctionLibrary::SpawnSystemAttachedWithParams(SpawnParams);
	return SpawnedComponent;
}

void UGMC_AbilitySystemComponent::MC_SpawnParticleSystemAttached_Implementation(const FFXSystemSpawnParameters& SpawnParams, bool bIsClientPredicted, bool bDelayByGMCSmoothing)
{
	// Server already spawned
	if (HasAuthority()) return;
	
	// Owning client already spawned
	if (IsLocallyControlledPawnASC() && bIsClientPredicted) return;
	
	SpawnParticleSystemAttached(SpawnParams, bIsClientPredicted, bDelayByGMCSmoothing);
}

UNiagaraComponent* UGMC_AbilitySystemComponent::SpawnParticleSystemAtLocation(FFXSystemSpawnParameters SpawnParams, bool bIsClientPredicted,
	bool bDelayByGMCSmoothing)
{
	if (SpawnParams.SystemTemplate == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to spawn FX, but FX is null!"));
		return nullptr;
	}
	
	if (SpawnParams.WorldContextObject == nullptr)
	{
		SpawnParams.WorldContextObject = GetWorld();
	}

	if (HasAuthority())
	{
		MC_SpawnParticleSystemAtLocation(SpawnParams, bIsClientPredicted, bDelayByGMCSmoothing);
	}

	// Sim Proxies can delay FX by the smoothing delay to better line up
	if (bDelayByGMCSmoothing && !HasAuthority() && !IsLocallyControlledPawnASC())
	{
		float Delay = GMCMovementComponent->GetTime() - GMCMovementComponent->GetSmoothingTime();
		FTimerHandle DelayHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayHandle, [this, SpawnParams]()
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocationWithParams(SpawnParams);
		}, Delay, false);

		UE_LOG(LogTemp, Warning, TEXT("Delay: %f"), Delay);

		return nullptr;
	}

	UNiagaraComponent* SpawnedComponent = UNiagaraFunctionLibrary::SpawnSystemAtLocationWithParams(SpawnParams);
	return SpawnedComponent;
}

void UGMC_AbilitySystemComponent::MC_SpawnParticleSystemAtLocation_Implementation(const FFXSystemSpawnParameters& SpawnParams,
	bool bIsClientPredicted, bool bDelayByGMCSmoothing)
{
	// Server already spawned
	if (HasAuthority()) return;
	
	// Owning client already spawned
	if (IsLocallyControlledPawnASC() && bIsClientPredicted) return;
	
	SpawnParticleSystemAtLocation(SpawnParams, bIsClientPredicted, bDelayByGMCSmoothing);
}

void UGMC_AbilitySystemComponent::SpawnSound(USoundBase* Sound, const FVector Location, const float VolumeMultiplier, const float PitchMultiplier, const bool bIsClientPredicted)
{
	// Spawn sound
	if (Sound == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to spawn sound, but sound is null!"));
		return;
	}

	if (HasAuthority())
	{
		MC_SpawnSound(Sound, Location, VolumeMultiplier, PitchMultiplier, bIsClientPredicted);
	}

	// Spawn Sound At Location
	UGameplayStatics::PlaySoundAtLocation(GetWorld(), Sound, Location, VolumeMultiplier, PitchMultiplier);
}

void UGMC_AbilitySystemComponent::MC_SpawnSound_Implementation(USoundBase* Sound, const FVector Location, const float VolumeMultiplier, const float PitchMultiplier,
	bool bIsClientPredicted)
{
	// Server already spawned
	if (HasAuthority()) return;

	if (IsLocallyControlledPawnASC() && bIsClientPredicted) return;
	SpawnSound(Sound, Location, VolumeMultiplier, PitchMultiplier, bIsClientPredicted);
}

// ReplicatedProps
void UGMC_AbilitySystemComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGMC_AbilitySystemComponent, UnBoundAttributes);
}

bool UGMC_AbilitySystemComponent::IsReplayingForGMASLogic() const
{
#if WITH_AUTOMATION_WORKER
	if (bForceReplayingForTest) { return true; }
#endif
	return GMCMovementComponent && GMCMovementComponent->CL_IsReplaying();
}

bool UGMC_AbilitySystemComponent::IsAuthorityForGMASLogic() const
{
#if WITH_AUTOMATION_WORKER
	if (bForceAuthorityForTest) { return true; }
#endif
	return HasAuthority();
}

void UGMC_AbilitySystemComponent::ProcessReplayBurstDiagnostic()
{
	const UGMASReplayBurstSettings* Settings = GetDefault<UGMASReplayBurstSettings>();
	if (!Settings || !Settings->bEnableDetection)
	{
		// Diagnostic disabled — keep the buffer empty so a re-enable doesn't
		// fire on stale timestamps from a previous session.
		RecentReplayTimestamps.Reset();
		return;
	}

	const double Now      = FPlatformTime::Seconds();
	const double Cutoff   = Now - static_cast<double>(Settings->WindowSeconds);

	// Append the new occurrence + drop entries that fell out of the window.
	RecentReplayTimestamps.Add(Now);
	RecentReplayTimestamps.RemoveAll([Cutoff](double T) { return T < Cutoff; });

	if (RecentReplayTimestamps.Num() < Settings->BurstThreshold)
	{
		return;
	}

	// Threshold tripped. Snapshot the count BEFORE clearing — callers want the
	// number of occurrences that produced the alert, not 0.
	const int32 BurstCount    = RecentReplayTimestamps.Num();
	const float WindowSeconds = Settings->WindowSeconds;
	RecentReplayTimestamps.Reset();

	UE_LOG(LogGMCAbilitySystem, Warning,
		TEXT("[ReplayBurst] %s observed %d replay occurrences within %.2fs (threshold=%d). ")
		TEXT("Likely a sustained validation divergence — check recent attribute mutations or bound state writes."),
		*GetNameSafe(GetOwner()), BurstCount, WindowSeconds, Settings->BurstThreshold);

	OnReplayBurstDetected.Broadcast(BurstCount, WindowSeconds);

	if (Settings->WarningWidgetClass.IsValid() || Settings->WarningWidgetClass.ToSoftObjectPath().IsValid())
	{
		TryShowReplayBurstWarningWidget();
	}
}

void UGMC_AbilitySystemComponent::TryShowReplayBurstWarningWidget()
{
	const UGMASReplayBurstSettings* Settings = GetDefault<UGMASReplayBurstSettings>();
	if (!Settings) { return; }

	// Resolve the local PC. If the owner pawn isn't possessed by a local PC,
	// there's no viewport to attach to and we silently skip — the delegate +
	// log already fired, so nothing is lost.
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!OwnerPawn) { return; }
	APlayerController* PC = Cast<APlayerController>(OwnerPawn->GetController());
	if (!PC || !PC->IsLocalController()) { return; }

	// If a previous widget is still on screen, drop it before stacking another.
	// The user wants ONE warning visible at a time, not a pile-up during chains.
	if (UUserWidget* Existing = ActiveReplayWarningWidget.Get())
	{
		Existing->RemoveFromParent();
		ActiveReplayWarningWidget.Reset();
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReplayWarningWidgetTimerHandle);
	}

	// Lazy-load the soft class. LoadSynchronous because we're already in a slow
	// path (the alert just fired) and we want the widget visible this frame.
	UClass* WidgetClass = Settings->WarningWidgetClass.LoadSynchronous();
	if (!WidgetClass)
	{
		UE_LOG(LogGMCAbilitySystem, Warning,
			TEXT("[ReplayBurst] WarningWidgetClass is set but could not be loaded ('%s'). Skipping widget display."),
			*Settings->WarningWidgetClass.ToString());
		return;
	}

	UUserWidget* Widget = CreateWidget<UUserWidget>(PC, WidgetClass);
	if (!Widget) { return; }

	Widget->AddToPlayerScreen(Settings->WidgetZOrder);
	ActiveReplayWarningWidget = Widget;

	// Auto-remove after the configured duration. <=0 means "leave it persistent —
	// the next burst will replace it (see RemoveFromParent above)".
	if (Settings->WidgetDurationSeconds > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			TWeakObjectPtr<UUserWidget>           WeakWidget(Widget);
			TWeakObjectPtr<UGMC_AbilitySystemComponent> WeakSelf(this);
			World->GetTimerManager().SetTimer(
				ReplayWarningWidgetTimerHandle,
				FTimerDelegate::CreateLambda([WeakWidget, WeakSelf]()
				{
					if (UUserWidget* W = WeakWidget.Get())
					{
						W->RemoveFromParent();
					}
					if (UGMC_AbilitySystemComponent* Self = WeakSelf.Get())
					{
						Self->ActiveReplayWarningWidget.Reset();
					}
				}),
				Settings->WidgetDurationSeconds,
				/*bLoop=*/false);
		}
	}
}

