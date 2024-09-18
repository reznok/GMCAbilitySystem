// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"

#include "GMCAbilitySystem.h"
#include "GMCOrganicMovementComponent.h"
#include "GMCPlayerController.h"
#include "Ability/GMCAbility.h"
#include "Ability/GMCAbilityMapData.h"
#include "Attributes/GMCAttributesData.h"
#include "Effects/GMCAbilityEffect.h"
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
	if (!Handle.IsValid())
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Passed an invalid delegate to unbind for tag changes on %s"), *Tags.ToString())
		return;
	}
	
	for (int32 Index = FilteredTagDelegates.Num() - 1; Index >= 0; --Index)
	{
		TPair<FGameplayTagContainer, FGameplayTagFilteredMulticastDelegate>& SearchPair = FilteredTagDelegates[Index];
		if (SearchPair.Key == Tags)
		{
			if (!SearchPair.Value.Remove(Handle))
			{
				UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Unable to unbind a tag change delegate for %s"), *Tags.ToString())
			}
			
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
		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.BaseValue,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
		
		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.Value,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);

		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.AdditiveModifier,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
		
		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.MultiplyModifier,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
		
		GMCMovementComponent->BindSinglePrecisionFloat(AttributeForBind.DivisionModifier,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
	}
	
	// Sync'd Action Timer
	GMCMovementComponent->BindDoublePrecisionFloat(ActionTimer,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

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

	// AbilityData Binds
	// These are mostly client-inputs made to the server as Ability Requests
	GMCMovementComponent->BindInt(AbilityData.AbilityActivationID,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	GMCMovementComponent->BindGameplayTag(AbilityData.InputTag,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
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

	GMCMovementComponent->BindInstancedStruct(AcknowledgeId,
	EGMC_PredictionMode::ClientAuth_Input,
	EGMC_CombineMode::CombineIfUnchanged,
	EGMC_SimulationMode::None,
	EGMC_InterpolationFunction::TargetValue);
	
}
void UGMC_AbilitySystemComponent::GenAncillaryTick(float DeltaTime, bool bIsCombinedClientMove)
{

	OnAncillaryTick.Broadcast(DeltaTime);

	ClientHandlePendingEffect();
	ServerHandlePendingEffect(DeltaTime);
	
	CheckActiveTagsChanged();
	CheckAttributeChanged();

	TickActiveEffects(DeltaTime);
	TickActiveCooldowns(DeltaTime);
	TickAncillaryActiveAbilities(DeltaTime);
	
	
	// Activate abilities from ancillary tick if they have bActivateOnMovementTick set to false
	if (AbilityData.InputTag != FGameplayTag::EmptyTag)
	{
		TryActivateAbilitiesByInputTag(AbilityData.InputTag, AbilityData.ActionInput, false);
	}

	SendTaskDataToActiveAbility(false);
	
	ClearAbilityAndTaskData();
	bInGMCTime = false;
}


TArray<UGMCAbilityEffect*> UGMC_AbilitySystemComponent::GetActivesEffectByTag(FGameplayTag GameplayTag) const {
	TArray<UGMCAbilityEffect*> ActiveEffectsFound;

	UE_LOG(LogGMCAbilitySystem, Error, TEXT("Searching for Active Effects with Tag: %s"), *GameplayTag.ToString());
	
	for (const TTuple<int, UGMCAbilityEffect*>& EffectFound : ActiveEffects) {
		if (IsValid(EffectFound.Value) && EffectFound.Value->EffectData.EffectTag.MatchesTag(GameplayTag)) {
			ActiveEffectsFound.Add(EffectFound.Value);
		}
	}

	return ActiveEffectsFound;
}


UGMCAbilityEffect* UGMC_AbilitySystemComponent::GetFirstActiveEffectByTag(FGameplayTag GameplayTag) const {
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
	for (const TSubclassOf<UGMCAbilityEffect> Effect : EffectsToAdd)
	{
		StartingEffects.AddUnique(Effect);
	}
}

void UGMC_AbilitySystemComponent::RemoveStartingEffects(TArray<TSubclassOf<UGMCAbilityEffect>> EffectsToRemove)
{
	for (const TSubclassOf<UGMCAbilityEffect> Effect : EffectsToRemove)
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

void UGMC_AbilitySystemComponent::TryActivateAbilitiesByInputTag(const FGameplayTag& InputTag, const UInputAction* InputAction, bool bFromMovementTick)
{
	
	for (const TSubclassOf<UGMCAbility>& ActivatedAbility : GetGrantedAbilitiesByTag(InputTag))
	{
		const UGMCAbility* AbilityCDO = ActivatedAbility->GetDefaultObject<UGMCAbility>();
		if(AbilityCDO && bFromMovementTick == AbilityCDO->bActivateOnMovementTick){
			UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("Trying to Activate Ability: %s from %s"), *GetNameSafe(ActivatedAbility), bFromMovementTick ? TEXT("Movement") : TEXT("Ancillary"));
			TryActivateAbility(ActivatedAbility, InputAction);
		}
	}
}

bool UGMC_AbilitySystemComponent::TryActivateAbility(const TSubclassOf<UGMCAbility> ActivatedAbility, const UInputAction* InputAction)
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
	
	Ability->Execute(this, AbilityID, InputAction);
	ActiveAbilities.Add(AbilityID, Ability);
	
	if (HasAuthority()) {RPCConfirmAbilityActivation(AbilityID);}
	
	return true;
}

void UGMC_AbilitySystemComponent::QueueAbility(FGameplayTag InputTag, const UInputAction* InputAction)
{
	if (GetOwnerRole() != ROLE_AutonomousProxy && GetOwnerRole() != ROLE_Authority) return;

	FGMCAbilityData Data;
	Data.InputTag = InputTag;
	Data.ActionInput = InputAction;
	QueuedAbilities.Push(Data);
}

int32 UGMC_AbilitySystemComponent::GetQueuedAbilityCount(FGameplayTag AbilityTag)
{
	int32 Result = 0;

	for (const auto& QueuedData : QueuedAbilities)
	{
		if (QueuedData.InputTag == AbilityTag) Result++;
	}
	return Result;
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
				if (Tag.MatchesTag(AbilityTag)) {
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
			ActiveAbilityData.Value->SetPendingEnd();
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

void UGMC_AbilitySystemComponent::GenPredictionTick(float DeltaTime)
{
	bJustTeleported = false;
	ActionTimer += DeltaTime;
	
	ApplyStartingEffects();
	
	TickActiveAbilities(DeltaTime);
	
	// Abilities
	CleanupStaleAbilities();
	
	// Was an ability used?
	if (AbilityData.InputTag != FGameplayTag::EmptyTag)
	{
		TryActivateAbilitiesByInputTag(AbilityData.InputTag, AbilityData.ActionInput, true);
	}

	SendTaskDataToActiveAbility(true);

	
}

void UGMC_AbilitySystemComponent::GenSimulationTick(float DeltaTime)
{
	CheckActiveTagsChanged();
	CheckAttributeChanged();
	
	if (GMCMovementComponent->GetSmoothingTargetIdx() == -1) return;	
	const FVector TargetLocation = GMCMovementComponent->MoveHistory[GMCMovementComponent->GetSmoothingTargetIdx()].OutputState.ActorLocation.Read();
	if (bJustTeleported)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Teleporting %f Units"), FVector::Distance(GetOwner()->GetActorLocation(), TargetLocation));
		GetOwner()->SetActorLocation(TargetLocation);
		bJustTeleported = false;
	}
}

void UGMC_AbilitySystemComponent::PreLocalMoveExecution()
{
	if (QueuedAbilities.Num() > 0)
	{
		AbilityData = QueuedAbilities.Pop();
	}
	if (QueuedTaskData.Num() > 0)
	{
		TaskData = QueuedTaskData.Pop();
	}

	
}

void UGMC_AbilitySystemComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeStartingAbilities();
	InitializeAbilityMap();
	SetStartingTags();
}

void UGMC_AbilitySystemComponent::InstantiateAttributes()
{
	BoundAttributes = FGMCAttributeSet();
	UnBoundAttributes = FGMCUnboundAttributeSet();
	if(AttributeDataAssets.IsEmpty()) return;

	// Loop through each of the data assets inputted into the component to create new attributes.
	for(UGMCAttributesData* AttributeDataAsset : AttributeDataAssets){

		// Avoid crashing in an editor preview if we're actually editing the ability component's attribute table.
		if (!AttributeDataAsset) continue;
		
		for(const FAttributeData AttributeData : AttributeDataAsset->AttributeData){
			FAttribute NewAttribute;
			NewAttribute.Tag = AttributeData.AttributeTag;
			NewAttribute.BaseValue = AttributeData.DefaultValue;
			NewAttribute.Clamp = AttributeData.Clamp;
			NewAttribute.Clamp.AbilityComponent = this;
			NewAttribute.bIsGMCBound = AttributeData.bGMCBound;
			NewAttribute.Init();
			
			if(AttributeData.bGMCBound){
				BoundAttributes.AddAttribute(NewAttribute);
			}
			else if (GetOwnerRole() == ROLE_Authority || GetNetMode() == NM_Standalone) {
				// FFastArraySerializer will duplicate all attributes on first replication if we
				// add the attributes on the clients as well.
				UnBoundAttributes.AddAttribute(NewAttribute);
				
			}
			
			if (!AttributeData.bGMCBound) {
				// Initiate old unbound attributes
				OldUnBoundAttributes.AddAttribute(NewAttribute);
			}
		}
	}

	// After all attributes are initialized, calc their values which will primarily apply their Clamps
	
	for (const FAttribute& Attribute : BoundAttributes.Attributes)
	{
		Attribute.CalculateValue();
	}

	// We need to be non-const to ensure we can mark the item dirty.
	for (FAttribute& Attribute : UnBoundAttributes.Items)
	{
		Attribute.CalculateValue();
		UnBoundAttributes.MarkItemDirty(Attribute);
	}
	UnBoundAttributes.MarkArrayDirty();

	for (const FAttribute& Attribute : OldUnBoundAttributes.Items)
	{
		Attribute.CalculateValue();
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
		
		if (!IsValid(Effect.Value)) {
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Active Effect id %d is null or pending kill, removing from the list."), Effect.Key);
			CompletedActiveEffects.Push(Effect.Key);
			continue;	
		}
		
		Effect.Value->Tick(DeltaTime);
		if (Effect.Value->bCompleted) {CompletedActiveEffects.Push(Effect.Key);}

		// Check for predicted effects that have not been server confirmed
		if (!HasAuthority() &&
			ProcessedEffectIDs.Contains(Effect.Key) &&
			!ProcessedEffectIDs[Effect.Key] && Effect.Value->ClientEffectApplicationTime + ClientEffectApplicationTimeout < ActionTimer)
		{
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
		ActiveEffectsData.RemoveAll([EffectID](const FGMCAbilityEffectData& EffectData) {return EffectData.EffectID == EffectID;});
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

void UGMC_AbilitySystemComponent::OnRep_ActiveEffectsData()
{
	for (FGMCAbilityEffectData ActiveEffectData : ActiveEffectsData)
	{
		if (ActiveEffectData.EffectID == 0) continue;
		
		if (!ProcessedEffectIDs.Contains(ActiveEffectData.EffectID))
		{
			UGMCAbilityEffect* EffectCDO = DuplicateObject(UGMCAbilityEffect::StaticClass()->GetDefaultObject<UGMCAbilityEffect>(), this);
			FGMCAbilityEffectData EffectData = ActiveEffectData;
			ApplyAbilityEffect(EffectCDO, EffectData);
			ProcessedEffectIDs.Add(EffectData.EffectID, true);
			UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("Replicated Effect: %d"), ActiveEffectData.EffectID);
		}
		
		ProcessedEffectIDs[ActiveEffectData.EffectID] = true;
	}
}

void UGMC_AbilitySystemComponent::CheckRemovedEffects()
{
	for (TPair<int, UGMCAbilityEffect*> Effect : ActiveEffects)
	{
		// Ensure this effect has been processed locally
		if (!ProcessedEffectIDs.Contains(Effect.Key)){return;}

		// Ensure this effect has already been confirmed by the server so that if it's now missing,
		// it means the server removed it
		if (!ProcessedEffectIDs[Effect.Key]){return;}
		
		if (!ActiveEffectsData.ContainsByPredicate([Effect](const FGMCAbilityEffectData& EffectData) {return EffectData.EffectID == Effect.Key;}))
		{
			RemoveActiveAbilityEffect(Effect.Value);
		}
	}
}

void UGMC_AbilitySystemComponent::AddPendingEffectApplications(FGMCOuterApplicationWrapper& Wrapper) {
	check(HasAuthority())

	Wrapper.ClientGraceTimeRemaining = 1.f;
	Wrapper.LateApplicationID = GenerateLateApplicationID();

	PendingApplicationServer.Add(Wrapper);
	RPCClientAddPendingEffectApplication(Wrapper);
}


void UGMC_AbilitySystemComponent::RPCClientAddPendingEffectApplication_Implementation(
		FGMCOuterApplicationWrapper Wrapper) {
	PendingApplicationClient.Add(Wrapper);
}



void UGMC_AbilitySystemComponent::ServerHandlePendingEffect(float DeltaTime) {
	if (!HasAuthority()) {
		return;
	}

	FGMCAcknowledgeId& AckId =	AcknowledgeId.GetMutable<FGMCAcknowledgeId>();


	for (int i = PendingApplicationServer.Num() - 1; i >= 0; i--) {
		FGMCOuterApplicationWrapper& Wrapper = PendingApplicationServer[i];

		if (Wrapper.ClientGraceTimeRemaining <= 0.f || AckId.Id.Contains(Wrapper.LateApplicationID)) {

			switch (Wrapper.Type) {
				case EGMC_AddEffect: {
					const FGMCOuterEffectAdd& Data = Wrapper.OuterApplicationData.Get<FGMCOuterEffectAdd>();
					UGMCAbilityEffect* AbilityEffect = DuplicateObject(Data.EffectClass->GetDefaultObject<UGMCAbilityEffect>(), this);
					AbilityEffect->EffectData.EffectID = Wrapper.LateApplicationID;
					FGMCAbilityEffectData EffectData = Data.InitializationData.IsValid() ?  Data.InitializationData : AbilityEffect->EffectData;
					UGMCAbilityEffect* FX = ApplyAbilityEffect(AbilityEffect, EffectData);
					if (Wrapper.ClientGraceTimeRemaining <= 0.f) {
						UE_LOG(LogGMCAbilitySystem, Log, TEXT("Client Add Effect `%s ` Missed Grace time, Force application : id: %d"), *GetNameSafe(Data.EffectClass), FX->EffectData.EffectID);
					}
				} break;
				case EGMC_RemoveEffect: {
					const FGMCOuterEffectRemove& Data = Wrapper.OuterApplicationData.Get<FGMCOuterEffectRemove>();
					RemoveEffectById(Data.Ids);
					if (Wrapper.ClientGraceTimeRemaining <= 0.f) {
						UE_LOG(LogGMCAbilitySystem, Log, TEXT("Client Remove Effect Missed Grace time, Force remove"));
					}
				} break;
			}
			PendingApplicationServer.RemoveAt(i);
		}
		else {
			Wrapper.ClientGraceTimeRemaining -= DeltaTime;
		}

		
	}
	
}


void UGMC_AbilitySystemComponent::ClientHandlePendingEffect() {


	for (int i = PendingApplicationClient.Num() - 1; i >= 0; i--)
	{
		FGMCOuterApplicationWrapper& LateApplicationData = PendingApplicationClient[i];

			switch (LateApplicationData.Type) {
				case EGMC_AddEffect: {
						const FGMCOuterEffectAdd& Data = LateApplicationData.OuterApplicationData.Get<FGMCOuterEffectAdd>();

						if (Data.EffectClass == nullptr) {
							UE_LOG(LogGMCAbilitySystem, Error, TEXT("ClientHandlePendingEffect: EffectClass is null"));
							break;
						}
					
						UGMCAbilityEffect* CDO = Data.EffectClass->GetDefaultObject<UGMCAbilityEffect>();

						if (CDO == nullptr) {
							UE_LOG(LogGMCAbilitySystem, Error, TEXT("ClientHandlePendingEffect: CDO is null"));
							break;
						}
					
						UGMCAbilityEffect* AbilityEffect = DuplicateObject(CDO, this);
						AbilityEffect->EffectData.EffectID = LateApplicationData.LateApplicationID;
						FGMCAbilityEffectData EffectData = Data.InitializationData.IsValid() ?  Data.InitializationData : AbilityEffect->EffectData;
						ApplyAbilityEffect(AbilityEffect, EffectData);
					} break;
				case EGMC_RemoveEffect: {
						const FGMCOuterEffectRemove& Data = LateApplicationData.OuterApplicationData.Get<FGMCOuterEffectRemove>();
						RemoveEffectById(Data.Ids);
					} break;
			}
		
			PendingApplicationClient.RemoveAt(i);
			AcknowledgeId.GetMutable<FGMCAcknowledgeId>().Id.Add(LateApplicationData.LateApplicationID);
		}
}


int UGMC_AbilitySystemComponent::GenerateLateApplicationID() {
	int NewEffectID = static_cast<int>(ActionTimer * 100);
	while (ActiveEffects.Contains(NewEffectID))
	{
		NewEffectID++;
	}

	return NewEffectID;
}


void UGMC_AbilitySystemComponent::RPCTaskHeartbeat_Implementation(int AbilityID, int TaskID)
{
	if (ActiveAbilities.Contains(AbilityID) && ActiveAbilities[AbilityID] != nullptr)
	{
		ActiveAbilities[AbilityID]->HandleTaskHeartbeat(TaskID);
	}
}

void UGMC_AbilitySystemComponent::RPCClientEndEffect_Implementation(int EffectID)
{
	if (ActiveEffects.Contains(EffectID))
	{
		ActiveEffects[EffectID]->EndEffect();
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[RPC] Server Ended Effect: %d"), EffectID);
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
				ApplyAbilityEffect(Effect, FGMCAbilityEffectData{});
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
	TaskData = FInstancedStruct::Make(FGMCAbilityTaskData{});
}


void UGMC_AbilitySystemComponent::SendTaskDataToActiveAbility(bool bFromMovement) {
	
	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.Get<FGMCAbilityTaskData>();
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

	return true;
	
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
	{
		if (GrantedAbilityTags.HasTag(AbilityMapData.InputTag))
		{
			GrantedAbilityTags.RemoveTag(AbilityMapData.InputTag);
		}
	}
}

void UGMC_AbilitySystemComponent::InitializeStartingAbilities()
{
	for (FGameplayTag Tag : StartingAbilities)
	{
		GrantedAbilityTags.AddTag(Tag);
	}
}

void UGMC_AbilitySystemComponent::OnRep_UnBoundAttributes()
{
	
	if (OldUnBoundAttributes.Items.Num() != UnBoundAttributes.Items.Num())
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("OnRep_UnBoundAttributes: Mismatched Attribute Old != New Value !"));
	}

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
			UnBoundAttributes.MarkAttributeDirty(Attribute);

			// Update Old Value
			*OldValues[Attribute.Tag] = Attribute.Value;
		}
	}

	
}

//BP Version
UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, FGMCAbilityEffectData InitializationData, bool bOuterActivation)
{
	if (Effect == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to apply Effect, but effect is null!"));
		return nullptr;
	}

	
	// We are trying to apply an effect from an outside source, so we will need to go trough a different routing to apply it
	if (bOuterActivation) {
		if (HasAuthority()) {
			FGMCOuterApplicationWrapper Wrapper = FGMCOuterApplicationWrapper::Make<FGMCOuterEffectAdd>(Effect, InitializationData);
			AddPendingEffectApplications(Wrapper);
		}
		return nullptr;
	}
	

	
	UGMCAbilityEffect* AbilityEffect = DuplicateObject(Effect->GetDefaultObject<UGMCAbilityEffect>(), this);
	
	FGMCAbilityEffectData EffectData;
	if (InitializationData.IsValid())
	{
		EffectData = InitializationData;
	}
	else
	{
		EffectData = AbilityEffect->EffectData;
	}
	
	ApplyAbilityEffect(AbilityEffect, EffectData);
	return AbilityEffect;
}

UGMCAbilityEffect* UGMC_AbilitySystemComponent::ApplyAbilityEffect(UGMCAbilityEffect* Effect, FGMCAbilityEffectData InitializationData)
{
	if (Effect == nullptr) {
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Trying to apply Effect, but effect is null!"));
		return nullptr;
	}
	
	
	// Force the component this is being applied to to be the owner
	InitializationData.OwnerAbilityComponent = this;
	
	Effect->InitializeEffect(InitializationData);
	
	if (Effect->EffectData.EffectID == 0)
	{
		if (ActionTimer == 0)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("[ApplyAbilityEffect] Action Timer is 0, cannot generate Effect ID. Is it a listen server smoothed pawn?"));
			return nullptr;
		}
		
		int NewEffectID = static_cast<int>(ActionTimer * 100);
		while (ActiveEffects.Contains(NewEffectID))
		{
			NewEffectID++;
		}
		Effect->EffectData.EffectID = NewEffectID;
		UE_LOG(LogGMCAbilitySystem, VeryVerbose, TEXT("[Server: %hhd] Generated Effect ID: %d"), HasAuthority(), Effect->EffectData.EffectID);
	}

	// This is Replicated, so only server needs to manage it
	if (HasAuthority())
	{
		ActiveEffectsData.Push(Effect->EffectData);
	}
	else
	{
		ProcessedEffectIDs.Add(Effect->EffectData.EffectID, false);
	}
	
	ActiveEffects.Add(Effect->EffectData.EffectID, Effect);
	return Effect;
}

void UGMC_AbilitySystemComponent::RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect)
{
	if (Effect == nullptr)
	{
		return;
	}
	
	if (!ActiveEffects.Contains(Effect->EffectData.EffectID)) return;
	Effect->EndEffect();
}

int32 UGMC_AbilitySystemComponent::RemoveEffectByTag(FGameplayTag InEffectTag, int32 NumToRemove, bool bOuterActivation) {
	
	if (NumToRemove < -1 || !InEffectTag.IsValid()) {
		return 0;
	}

	TMap<int, UGMCAbilityEffect*> EffectsToRemove;
	int32 NumRemoved = 0;
	
	for(const TTuple<int, UGMCAbilityEffect*> Effect : ActiveEffects)
	{
		if(NumRemoved == NumToRemove){
			break;
		}
		
		if(Effect.Value->EffectData.EffectTag.IsValid() && Effect.Value->EffectData.EffectTag.MatchesTagExact(InEffectTag)){
			EffectsToRemove.Add(Effect.Key, Effect.Value);
			NumRemoved++;
		}
	}
	

	if (bOuterActivation) {
		if (HasAuthority() && EffectsToRemove.Num() > 0) {

			TArray<int> EffectIDsToRemove;
			for (const auto& ToRemove : EffectsToRemove) {
				EffectIDsToRemove.Add(ToRemove.Key);
			}
			
			FGMCOuterApplicationWrapper Wrapper = FGMCOuterApplicationWrapper::Make<FGMCOuterEffectRemove>(EffectIDsToRemove);
			AddPendingEffectApplications(Wrapper);
		}
		return 0;
	}

	for (auto& ToRemove : EffectsToRemove) {
		ToRemove.Value->EndEffect();
	}
	
	return NumRemoved;
}


bool UGMC_AbilitySystemComponent::RemoveEffectById(TArray<int> Ids, bool bOuterActivation) {

	if (!Ids.Num()) {
		return true;
	}

	// check all IDs exists
	for (int Id : Ids) {
		if (!ActiveEffects.Contains(Id)) {
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Trying to remove effect with ID %d, but it doesn't exist!"), Id);
			return false;
		}
	}

	if (bOuterActivation) {
		if (HasAuthority()) {
			FGMCOuterApplicationWrapper Wrapper = FGMCOuterApplicationWrapper::Make<FGMCOuterEffectRemove>(Ids);
			AddPendingEffectApplications(Wrapper);
		}
		return true;
	}

	for (auto& Effect : ActiveEffects) {
		if (Ids.Contains(Effect.Key)) {
			Effect.Value->EndEffect();
		}
	}

	return true;
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
	for (const FAttribute& Attribute : UnBoundAttributes.Items){
		AllAttributes.Add(&Attribute);
	}

	for (const FAttribute& Attribute : BoundAttributes.Attributes){
		AllAttributes.Add(&Attribute);
	}
	return AllAttributes;
}

const FAttribute* UGMC_AbilitySystemComponent::GetAttributeByTag(FGameplayTag AttributeTag) const
{
	if (AttributeTag == FGameplayTag::EmptyTag) return nullptr;
	
	if(!AttributeTag.IsValid()){
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Tried to get an attribute with an invalid tag!"))
		return nullptr;
	}
	TArray<const FAttribute*> AllAttributes = GetAllAttributes();
	const FAttribute** FoundAttribute = AllAttributes.FindByPredicate([AttributeTag](const FAttribute* Attribute){
		return Attribute->Tag.MatchesTagExact(AttributeTag);
	});
	
	if(FoundAttribute && *FoundAttribute){
		return *FoundAttribute;
	}
	return nullptr;
}

float UGMC_AbilitySystemComponent::GetAttributeValueByTag(const FGameplayTag AttributeTag) const
{
	if (const FAttribute* Att = GetAttributeByTag(AttributeTag))
	{
		return Att->Value;
	}
	return 0;
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
		Att->SetBaseValue(NewValue);

		if (bResetModifiers)
		{
			Att->ResetModifiers();
		}

		Att->CalculateValue();
		return true;
	}
	return false;
}

float UGMC_AbilitySystemComponent::GetBaseAttributeValueByTag(FGameplayTag AttributeTag) const{
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
		FinalString += Attribute->ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsDataString() const{
	FString FinalString = TEXT("\n");
	for(const FGMCAbilityEffectData& ActiveEffectData : ActiveEffectsData){
		FinalString += ActiveEffectData.ToString() + TEXT("\n");
	}
	return FinalString;
}

FString UGMC_AbilitySystemComponent::GetActiveEffectsString() const{
	FString FinalString = TEXT("\n");
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

void UGMC_AbilitySystemComponent::ApplyAbilityEffectModifier(FGMCAttributeModifier AttributeModifier, bool bModifyBaseValue, bool bNegateValue,  UGMC_AbilitySystemComponent* SourceAbilityComponent)
{
	// Provide an opportunity to modify the attribute modifier before applying it
	UGMCAttributeModifierContainer* AttributeModifierContainer = NewObject<UGMCAttributeModifierContainer>(this);
	AttributeModifierContainer->AttributeModifier = AttributeModifier;

	// Broadcast the event to allow modifications to happen before application
	OnPreAttributeChanged.Broadcast(AttributeModifierContainer, SourceAbilityComponent);

	// Apply the modified attribute modifier. If no changes were made, it's just the same as the original
	// Extra copying going on here? Can this be done with a reference? BPs are weird.
	AttributeModifier = AttributeModifierContainer->AttributeModifier;
	
	if (const FAttribute* AffectedAttribute = GetAttributeByTag(AttributeModifier.AttributeTag))
	{
		// If we are unbound that means we shouldn't predict.
		if(!AffectedAttribute->bIsGMCBound && !HasAuthority()) return;
		float OldValue = AffectedAttribute->Value;
		FGMCUnboundAttributeSet OldUnboundAttributes = UnBoundAttributes;
		
		if (bNegateValue)
		{
			AttributeModifier.Value = -AttributeModifier.Value;
		}
		AffectedAttribute->ApplyModifier(AttributeModifier, bModifyBaseValue);

		// Only broadcast a change if we've genuinely changed.
		if (OldValue != AffectedAttribute->Value)
		{
			OnAttributeChanged.Broadcast(AffectedAttribute->Tag, OldValue, AffectedAttribute->Value);
			NativeAttributeChangeDelegate.Broadcast(AffectedAttribute->Tag, OldValue, AffectedAttribute->Value);
		}

		BoundAttributes.MarkAttributeDirty(*AffectedAttribute);
		UnBoundAttributes.MarkAttributeDirty(*AffectedAttribute);
		if (!AffectedAttribute->bIsGMCBound) {
			OnRep_UnBoundAttributes();
		}
	}
}

// ReplicatedProps
void UGMC_AbilitySystemComponent::GetLifetimeReplicatedProps(TArray< FLifetimeProperty > & OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(UGMC_AbilitySystemComponent, ActiveEffectsData, COND_OwnerOnly);
	DOREPLIFETIME(UGMC_AbilitySystemComponent, UnBoundAttributes);
}


