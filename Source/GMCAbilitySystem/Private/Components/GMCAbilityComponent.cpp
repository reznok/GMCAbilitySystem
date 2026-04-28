// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"

#include "GMCAbilitySystem.h"
#include "GMCOrganicMovementComponent.h"
#include "GMCPlayerController.h"
#include "NiagaraFunctionLibrary.h"
#include "Ability/GMCAbility.h"
#include "Ability/GMCAbilityMapData.h"
#include "Attributes/GMCAttributesData.h"
#include "Effects/GMCAbilityEffect.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Net/UnrealNetwork.h"


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

void UGMC_AbilitySystemComponent::BindReplicationData()
{
	// Attribute Binds
	//
	InstantiateAttributes();

	// We sort our attributes alphabetically by tag so that it's deterministic.
	for (auto& AttributeForBind : BoundAttributes.Attributes)
	{
		AttributeForBind.BoundIndex = GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.Value,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.RawValue,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
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

	// Bind our operation queues.
	// QueuedAbilityOperations.BindToGMC(GMCMovementComponent);
	// QueuedEffectOperations.BindToGMC(GMCMovementComponent);
	// QueuedEffectOperations_ClientAuth.BindToGMC(GMCMovementComponent);
	// QueuedEventOperations.BindToGMC(GMCMovementComponent);
	BoundQueueV2.BindToGMC(GMCMovementComponent);
	
}
void UGMC_AbilitySystemComponent::GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UGMC_AbilitySystemComponent::GenAncillaryTick)
	
	// Caution if you override Ancillarytick, this value should wrap up the override.
	bInAncillaryTick = true;

	// Drain any PredictedQueued operations buffered since the last tick.
	DrainPendingPredictedOperations();

	OnAncillaryTick.Broadcast(DeltaTime);

	if (HasAuthority())
	{
		// Server processes client output payloads — only for REMOTE client pawns.
		// The listen server's own locally-controlled pawn does not submit "client
		// data" to itself, so SV_GetLastClientData().OutputState.InstancedStructs
		// is empty on the first frame, causing an out-of-bounds crash when
		// GetBoundInstancedStruct tries to access index BI_OperationData.
		if (GMCMovementComponent->IsPlayerControlledPawn() && !GMCMovementComponent->IsLocallyControlledListenServerPawn())
		{
			const FGMC_PawnState OutputState = GMCMovementComponent->SV_GetLastClientData().OutputState;
			const FInstancedStruct ClientPayloadOperationData = GMCMovementComponent->GetBoundInstancedStruct(BoundQueueV2.BI_OperationData, OutputState);
			ServerProcessOperation(ClientPayloadOperationData, false);
		}
		// Server owned pawns
		BoundQueueV2.GenPreLocalMoveExecution();
		ProcessOperation(BoundQueueV2.OperationData, false);
	}
	else
	{
		ProcessOperation(BoundQueueV2.OperationData, false);
	}
	
	CheckActiveTagsChanged();
	
	ProcessAttributes(false);
	
	CheckAttributeChanged();
	
	CheckUnBoundAttributeChanged();
	TickActiveCooldowns(DeltaTime);

	BoundQueueV2.GenAncillaryTick(DeltaTime);

	SendTaskDataToActiveAbility(false);
	TickAncillaryActiveAbilities(DeltaTime);
	
	ClearAbilityAndTaskData(); 
	
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

bool UGMC_AbilitySystemComponent::HasActiveTag(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTag(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasActiveTagExact(const FGameplayTag GameplayTag) const
{
	return ActiveTags.HasTagExact(GameplayTag);
}

bool UGMC_AbilitySystemComponent::HasAnyTag(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAny(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAnyTagExact(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAnyExact(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllTags(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAll(TagsToCheck);
}

bool UGMC_AbilitySystemComponent::HasAllTagsExact(const FGameplayTagContainer TagsToCheck) const
{
	return ActiveTags.HasAllExact(TagsToCheck);
}

TArray<FGameplayTag> UGMC_AbilitySystemComponent::GetActiveTagsByParentTag(const FGameplayTag ParentTag){
	TArray<FGameplayTag> MatchedTags;
	if(!ParentTag.IsValid()) return MatchedTags;
	for(FGameplayTag Tag : ActiveTags){
		if(Tag.MatchesTag(ParentTag)){
			MatchedTags.Add(Tag);
		}
	}
	return MatchedTags;
}

bool UGMC_AbilitySystemComponent::TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag, const UInputAction* InputAction,
	const bool bFromMovementTick, const bool bForce)
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
	
	for (const TSubclassOf<UGMCAbility>& ActivatedAbility : GrantedAbilities)
	{
		TryActivateAbility(ActivatedAbility, InputAction, InputTag);
	}

	return true;
}

bool UGMC_AbilitySystemComponent::TryActivateAbility(const TSubclassOf<UGMCAbility> ActivatedAbility, const UInputAction* InputAction, const FGameplayTag ActivationTag)
{
	
	if (ActivatedAbility == nullptr) return false;
	
	
	// Generated ID is based on ActionTimer so it always lines up on client/server
	// Also helps when dealing with replays
	int AbilityID = GenerateAbilityID();

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
	if (!CheckActivationTags(AbilityCDO)){
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Tags"), *GetNameSafe(ActivatedAbility));
		return false;
	}

	// If multiple abilities are activated on the same frame, add 1 to the ID
	// This should never actually happen as abilities get queued
	while (ActiveAbilities.Contains(AbilityID)){
		AbilityID += 1;
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
	
	if (HasAuthority()) {RPCConfirmAbilityActivation(AbilityID);}

	return true;
}

void UGMC_AbilitySystemComponent::QueueAbility(FGameplayTag InputTag, const UInputAction* InputAction, bool bPreventConcurrentActivation)
{
	if (GetOwnerRole() != ROLE_AutonomousProxy && GetOwnerRole() != ROLE_Authority) return;

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
	
	if (ActiveCooldowns.Contains(AbilityTag))
	{
		ActiveCooldowns[AbilityTag] = CooldownTime;
		return;
	}
	ActiveCooldowns.Add(AbilityTag, CooldownTime);
}

float UGMC_AbilitySystemComponent::GetCooldownForAbility(const FGameplayTag AbilityTag) const
{
	if (ActiveCooldowns.Contains(AbilityTag))
	{
		return ActiveCooldowns[AbilityTag];
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

	// Drain any PredictedQueued operations buffered since the last tick.
	DrainPendingPredictedOperations();

	// Same listen-server guard as GenAncillaryTick: skip client payload processing
	// for the locally-controlled host pawn (no client data submitted to itself).
	if (HasAuthority() && GMCMovementComponent->IsPlayerControlledPawn() && !GMCMovementComponent->IsLocallyControlledListenServerPawn())
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
	
	
	ApplyStartingEffects();

	// Purge "future" temporary modifiers on replay
	if (GMCMovementComponent->CL_IsReplaying())
	{
		for (FAttribute& Attribute : BoundAttributes.Attributes)
		{
			Attribute.PurgeTemporalModifier(ActionTimer);
		}
	}
	
	SendTaskDataToActiveAbility(true);
	TickActiveAbilities(DeltaTime);
	
	TickActiveEffects(DeltaTime);
	
	ProcessAttributes(true);

	// Abilities
	CleanupStaleAbilities();
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
		TaskData = QueuedTaskData.Pop();
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
		EffectActivationData.EffectID = GetNextAvailableEffectID();
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


void UGMC_AbilitySystemComponent::CleanupStaleAbilities()
{
	for (auto It = ActiveAbilities.CreateIterator(); It; ++It)
	{
		// If the contained ability is in the Ended state, delete it
		if (It.Value()->AbilityState == EAbilityState::Ended)
		{
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
	CheckRemovedEffects();
	
	TArray<int> CompletedActiveEffects;

	// Tick Effects
	for (const TPair<int, UGMCAbilityEffect*>& Effect : ActiveEffects)
	{
		
		if (!Effect.Value->IsValidLowLevel()) {
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Active Effect id %d is null or pending kill, removing from the list."), Effect.Key);
			CompletedActiveEffects.Push(Effect.Key);
			continue;	
		}
		
		Effect.Value->Tick(DeltaTime);
		if (Effect.Value->bCompleted)
		{
			CompletedActiveEffects.Push(Effect.Key);
		}

		// Check for predicted effects that have not been server confirmed
		if (!HasAuthority() &&
			!Effect.Value->EffectData.bServerAuth
			&& ProcessedEffectIDs.Contains(Effect.Key) 
			&& ProcessedEffectIDs[Effect.Key] == EGMCEffectAnswerState::Pending
			&& Effect.Value->ClientEffectApplicationTime + ClientEffectApplicationTimeout < ActionTimer)
		{
			ProcessedEffectIDs[Effect.Key] = EGMCEffectAnswerState::Timeout;
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Effect `%s` Not Confirmed By Server (ID: `%d`), Removing..."), *GetNameSafe(Effect.Value), Effect.Key);
			Effect.Value->EndEffect();
			CompletedActiveEffects.Push(Effect.Key);
		}
	}
	
	// Clean expired effects
	for (const int EffectID : CompletedActiveEffects)
	{
		// Notify client. Redundant.
		if (HasAuthority()) {RPCClientEndEffect(EffectID);}

		ActiveEffects.Remove(EffectID);
		ActiveEffectIDs.Remove(EffectID);
		ProcessedEffectIDs.Remove(EffectID);
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

void UGMC_AbilitySystemComponent::TickActiveCooldowns(float DeltaTime)
{
	for (auto It = ActiveCooldowns.CreateIterator(); It; ++It)
	{
		It.Value() -= DeltaTime;
		if (It.Value() <= 0)
		{
			It.RemoveCurrent();
		}
	}
}

void UGMC_AbilitySystemComponent::CheckRemovedEffects()
{
	TArray<int> EffectsToRemove;

	for (const TPair<int, UGMCAbilityEffect*>& Effect : ActiveEffects)
	{
		// Ensure this effect has been processed locally
		if (!ProcessedEffectIDs.Contains(Effect.Key)) { continue; }

		// Ensure this effect has already been confirmed by the server so that if it's now missing,
		// it means the server removed it
		if (ProcessedEffectIDs[Effect.Key] == EGMCEffectAnswerState::Pending) { continue; }

		// Replication grace: server-initiated effects (RPC-pushed via RPCOnServerOperationAdded) auto-validate
		// the moment they arrive on the client, but ActiveEffectIDs is DOREPLIFETIME-driven and lags ~1 RTT
		// behind. Without this guard we'd wipe the freshly applied effect on the same tick, before the replicated
		// list catches up — see "Recovery wipes itself" symptom (continuous chain replay on bound attributes).
		if (Effect.Value)
		{
			const float TimeSinceApply         = ActionTimer - Effect.Value->ClientEffectApplicationTime;
			const float ReplicationGracePeriod = Effect.Value->EffectData.ClientGraceTime;
			if (TimeSinceApply < ReplicationGracePeriod) { continue; }
		}

		// If the server's replicated effect list no longer contains this effect, remove it locally
		if (!ActiveEffectIDs.Contains(Effect.Key))
		{
			if (Effect.Value) { Effect.Value->EndEffect(); }
			EffectsToRemove.Add(Effect.Key);
		}
	}

	for (const int EffectID : EffectsToRemove)
	{
		if (HasAuthority()) { RPCClientEndEffect(EffectID); }
		ActiveEffects.Remove(EffectID);
		ActiveEffectIDs.Remove(EffectID);
		ProcessedEffectIDs.Remove(EffectID);
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
}

void UGMC_AbilitySystemComponent::RPCClientEndAbility_Implementation(int AbilityID)
{
	if (ActiveAbilities.Contains(AbilityID))
	{
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
	if (HasAuthority() && StartingEffects.Num() > 0 && (bForce || !bStartingEffectsApplied))
	{
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
		if (ActiveAbilities.Contains(TaskDataFromInstance.AbilityID) && ActiveAbilities[TaskDataFromInstance.AbilityID]->bActivateOnMovementTick == bFromMovement)
		{
			ActiveAbilities[TaskDataFromInstance.AbilityID]->HandleTaskData(TaskDataFromInstance.TaskID, TaskData);
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

	const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
	const int OperationID = BaseData->OperationID;

	if (OperationID == 0)
	{
		return false; // Empty/Default Operation, Ignore
	}
	
	// Payload data should be in the cache
	// Possible if it isn't with abilities being double processed anc/movement tick until that's checked later
	if (!BoundQueueV2.HasPayloadByID(OperationID))
	{
		// UE_LOG(LogGMCAbilitySystem, Error, TEXT("OperationID %d not found in OperationPayloads"), OperationID);
		return false;
	}

	// Pull actual payload from operation cache
	FInstancedStruct PayloadData = BoundQueueV2.GetPayloadByID(OperationID);

	// Server only ever processes operations once so it doesn't need them cached
	if (HasAuthority())
	{
		BoundQueueV2.RemovePayloadByID(OperationID);
	}
	
	const UScriptStruct* StructType = PayloadData.GetScriptStruct();

	// Activate Ability
	if (StructType == FGMASBoundQueueV2AbilityActivationOperation::StaticStruct())
	{
		const FGMASBoundQueueV2AbilityActivationOperation Data = PayloadData.Get<FGMASBoundQueueV2AbilityActivationOperation>();
		BoundQueueV2.OperationData  = PayloadData;
		return TryActivateAbilitiesByInputTag(Data.InputTag, Data.InputAction, bFromMovementTick, bForce);
	}

	// Everything below happens only during the Prediction tick
	if (!bFromMovementTick && !bForce){
		// BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(FGMASBoundQueueV2OperationBaseData{});
		return false;
	}

	///////////// Server-Auth Events
	// Todo: Every event is setting the same AcknowledgedOperation. Could probably pull that out of each event and just set it once at the end.
	//
	// Apply Effect
	if (StructType == FGMASBoundQueueV2ApplyEffectOperation::StaticStruct())
	{
		const FGMASBoundQueueV2ApplyEffectOperation Data = PayloadData.Get<FGMASBoundQueueV2ApplyEffectOperation>();
		ProcessEffectApplicationFromOperation(Data);
		if (!HasAuthority())
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
		
		if (!HasAuthority())
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
		if (!HasAuthority())
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
		if (!HasAuthority())
		{
			// Make an operation to confirm the location change
			BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2AcknowledgeOperation>(FGMASBoundQueueV2AcknowledgeOperation{OperationID});
		}
		return true;
	}

	// No valid event found, so nothing to ack. Send a blank.
	BoundQueueV2.OperationData = FInstancedStruct::Make<FGMASBoundQueueV2OperationBaseData>(FGMASBoundQueueV2OperationBaseData{});
	return false;
}

void UGMC_AbilitySystemComponent::ProcessEffectApplicationFromOperation(const FGMASBoundQueueV2ApplyEffectOperation& Data)
{
	if (Data.EffectClass)
	{
		UGMCAbilityEffect* Effect;
		int OutEffectHandle;
		int OutEffectId;
			
		if (Data.EffectData.IsValid())
		{
			ApplyAbilityEffect(Data.EffectClass, Data.EffectData, EGMCAbilityEffectQueueType::Predicted, OutEffectHandle, OutEffectId, Effect);
		}
		else
		{
			// Otherwise, we can apply the default effect data
			FGMCAbilityEffectData DefaultData = Data.EffectClass->GetDefaultObject<UGMCAbilityEffect>()->EffectData;
			DefaultData.EffectID = Data.EffectID; // Need to slam the effect ID in there
			ApplyAbilityEffect(Data.EffectClass,DefaultData, EGMCAbilityEffectQueueType::Predicted, OutEffectHandle, OutEffectId, Effect);
		}

		// Auto validate the effect since this was added via a server operation
		if (!HasAuthority() && Effect != nullptr)
		{
			ProcessedEffectIDs[Effect->EffectData.EffectID] = EGMCEffectAnswerState::Validated;
			UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("Applied Effect: %s"), *GetNameSafe(Data.EffectClass));
		}
	}
}

void UGMC_AbilitySystemComponent::ServerProcessOperation(const FInstancedStruct& OperationData, bool bFromMovementTick)
{
	if (!HasAuthority()) return;
	if (!BoundQueueV2.IsValidGMASOperation(OperationData)) return;

	const FGMASBoundQueueV2OperationBaseData* BaseData = OperationData.GetPtr<FGMASBoundQueueV2OperationBaseData>();
	const int OperationID = BaseData->OperationID;

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
		
		if (!BoundQueueV2.HasPayloadByID(OperationID))
		{
			BoundQueueV2.CacheOperationPayload(OperationID, OperationData);
		}
		
		ProcessOperation(OperationData, bFromMovementTick);
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

	TMap<FGameplayTag, float*> OldValues;

	// If this mitchmatch, that mean we need to reset the number of attributes
	
	for (FAttribute& Attribute : OldAttributes){
		OldValues.Add(Attribute.Tag, &Attribute.Value);
	}
	
	for (const FAttribute& Attribute : CurrentAttributes){
		if (OldValues.Contains(Attribute.Tag) && *OldValues[Attribute.Tag] != Attribute.Value){
			NativeAttributeChangeDelegate.Broadcast(Attribute.Tag, *OldValues[Attribute.Tag], Attribute.Value);
			OnAttributeChanged.Broadcast(Attribute.Tag, *OldValues[Attribute.Tag], Attribute.Value);

			// Update Old Value
			*OldValues[Attribute.Tag] = Attribute.Value;
		}
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
	while (ActiveEffects.Contains(NewEffectID) || ReservedEffectIDs.Contains(NewEffectID))
	{
		NewEffectID++;
	}
	UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[Server: %hhd] Generated Effect ID: %d"), HasAuthority(), NewEffectID);
	
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
			OutEffectId = OutEffect->EffectData.EffectID;
			// OutEffectHandle = HandleData.Handle;
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
			EffectActivationData.EffectID = GetNextAvailableEffectID();
			ReservedEffectIDs.Add(EffectActivationData.EffectID);

			// We return back the Operation ID instead of the Effect ID which isn't great
			OutEffectId =EffectActivationData.EffectID;
			int OperationID = BoundQueueV2.MakeOperationData<FGMASBoundQueueV2ApplyEffectOperation>(EffectActivationData);
			OutEffectHandle = OutEffectId;
			BoundQueueV2.QueueServerOperation(OperationID);
			return true;
		}

	case EGMCAbilityEffectQueueType::ClientAuth:
		return false;

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
	
	// Force the component this is being applied to to be the owner
	InitializationData.OwnerAbilityComponent = this;
	InitializationData.SourceAbilityComponent = this;
	
	Effect->InitializeEffect(InitializationData);
	
	if (Effect->EffectData.EffectID == 0)
	{
		Effect->EffectData.EffectID = GetNextAvailableEffectID();
	}

	// This is Replicated, so only server needs to manage it
	if (HasAuthority())
	{
		// If this was a server-auth, the ID is already generated and needs to be cleaned up from reserved
		ReservedEffectIDs.Remove(Effect->EffectData.EffectID);
		ActiveEffectIDs.Push(Effect->EffectData.EffectID); 
	}
	else
	{
		ProcessedEffectIDs.Add(Effect->EffectData.EffectID, EGMCEffectAnswerState::Pending);
	}

	ActiveEffects.Add(Effect->EffectData.EffectID, Effect);
	
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
	const bool bHasGracePeriod = Effect->EffectData.ClientGraceTime > 0.f;

	if (bIsNetworked && bIsTimeDriven && bHasGracePeriod && !Effect->bCompleted)
	{
		// First call arms the defer; subsequent calls during the defer window are no-ops.
		// Critical: the early return MUST happen even when defer is already armed, otherwise
		// a duplicate Remove (e.g. RPCClientEndEffect arriving while local defer is running)
		// would fall through to the EndEffect() below and defeat the bilateral synchronization.
		if (!Effect->bPendingPredictedEnd)
		{
			Effect->bPendingPredictedEnd     = true;
			Effect->PendingPredictedEndTimer = Effect->EffectData.ClientGraceTime;
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

	for (auto& [EffectId, Effect] : ActiveEffects)
	{
		if (IsValid(Effect) && Effect->EffectData.EffectTag.MatchesTag(Tag))
		{
			RemoveEffectByIdSafe({ EffectId }, QueueType);
			if (!bAllInstance) {
				return;
			}
		}
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
			return false;

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
	}

	ensureMsgf(false, TEXT("[%20s] %s attempted a removal of effects but something went horribly wrong! (%s)"),
		*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(), *GetEffectsNameAsString(GetEffectsByIds(Ids)));
	UE_LOG(LogGMCAbilitySystem, Error, TEXT("[%20s] %s attempted a removal of effects but something went horribly wrong! (%s)"),
		*GetNetRoleAsString(GetOwnerRole()), *GetOwner()->GetName(),*GetEffectsNameAsString(GetEffectsByIds(Ids)))
	return false;
}

bool UGMC_AbilitySystemComponent::RemoveEffectByHandle(int EffectHandle, EGMCAbilityEffectQueueType QueueType)
{
	int32 EffectID;
	UGMCAbilityEffect* Effect;
	if (GetEffectFromHandle(EffectHandle, EffectID, Effect) && EffectID > 0)
	{
		return RemoveEffectByIdSafe({ EffectID }, QueueType);
	}
	
	return false;	
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

//ActiveEffectIds OnRep
void UGMC_AbilitySystemComponent::OnRep_ActiveEffectIDs()
{
	// This is called when the ActiveEffectIDs array is replicated to the client
	// We need to update the ActiveEffects map based on the replicated IDs
	for (int EffectId : ActiveEffectIDs)
	{
		if (ProcessedEffectIDs.Contains(EffectId))
		{
			ProcessedEffectIDs[EffectId] = EGMCEffectAnswerState::Validated;
		}
	}
}

// ReplicatedProps
void UGMC_AbilitySystemComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UGMC_AbilitySystemComponent, UnBoundAttributes);
	DOREPLIFETIME(UGMC_AbilitySystemComponent, ActiveEffectIDs);
}

