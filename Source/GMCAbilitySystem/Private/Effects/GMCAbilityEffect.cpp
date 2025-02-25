// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"
#include "Kismet/KismetSystemLibrary.h"


FAbilityEffectSpec::FAbilityEffectSpec()
{
}

FAbilityEffectSpecForRPC::FAbilityEffectSpecForRPC()
{
}

void UGMCAbilityEffect::InitializeEffect(FGMCAbilityEffectData InitializationData)
{
	EffectData = InitializationData;
	OwnerAbilityComponent = EffectData.OwnerAbilityComponent;
	SourceAbilityComponent = EffectData.SourceAbilityComponent;
	

	if (OwnerAbilityComponent == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("OwnerAbilityComponent is null in UGMCAbilityEffect::InitializeEffect"));
		return;
	}
	
	AActor* OwningActor = OwnerAbilityComponent->GetOwner();
	if (OwningActor)
	{
		if (OwningActor->HasAuthority()) // Server
		{

				
		}
		else // Client
		{
			if(InitializationData.GameCues.IsEmpty())
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("NO CUES"));

			}else
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("CUES"));

			}
		}
	}


	EffectData.GameCues = GameCues;

	if (StackingType != EAbilityEffectStackingType::None)
	{

		if(StackingType == EAbilityEffectStackingType::AggregateByTarget){
		TArray<UGMCAbilityEffect*> ActiveEffects = OwnerAbilityComponent->GetActiveEffectsByTag(EffectData.EffectTag);

		if (ActiveEffects.Num() > 0)
		{
			// Apply stacking logic
			UGMCAbilityEffect* ExistingEffect = ActiveEffects[0];
			ExistingEffect->HandleStacking(this);
			return; // Exit early, as the new effect is merged into the existing one
		}
			}
	}


	
	
	ClientEffectApplicationTime = OwnerAbilityComponent->ActionTimer;

	// If server sends times, use those
	// Only used in the case of a non predicted effect
	if (InitializationData.StartTime != 0)
	{
		EffectData.StartTime = InitializationData.StartTime;
	}
	else
	{
		EffectData.StartTime = OwnerAbilityComponent->ActionTimer + EffectData.Delay;
	}
	
	if (InitializationData.EndTime != 0)
	{
		EffectData.EndTime = InitializationData.EndTime;
	}
	else
	{
		EffectData.EndTime = EffectData.StartTime + EffectData.Duration;
	}

	// Start Immediately
	if (EffectData.Delay == 0)
	{
		StartEffect();
	}
}

void UGMCAbilityEffect::HandleStacking(UGMCAbilityEffect* NewEffect)
{


	
	// Increment stack count
	if ( StackLimitCount > 0 && EffectStackCount < NewEffect->StackLimitCount && EffectStackCount < StackLimitCount )
	{
		int32 oldStack = EffectStackCount;
		EffectStackCount++;
		OnStackCountChange(this, oldStack,EffectStackCount);
		
	}

	EffectData.StackCount = EffectStackCount;

	
	// Apply stacking behavior to each modifier


	// Refresh duration or reset period based on stacking policies
	if (NewEffect->StackDurationRefreshPolicy == EAbilityEffectStackingDurationPolicy::RefreshOnSuccessfulApplication)
	{
		EffectData.EndTime = OwnerAbilityComponent->ActionTimer + EffectData.Duration;
	}

	if (NewEffect->StackPeriodResetPolicy == EAbilityEffectStackingPeriodPolicy::ResetOnSuccessfulApplication)
	{
		PrevPeriodMod = 0; // Reset period tracking
	}
}

void UGMCAbilityEffect::StartEffect()
{
	// Ensure tag requirements are met before applying the effect
	if( ( EffectData.ApplicationMustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustNotHaveTags) ||
	( EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
		return;
	}

	bHasStarted = true;
	
	AddTagsToOwner();
	AddAbilitiesToOwner();
	PlayQueue();
	EndActiveAbilitiesFromOwner();

	// Instant effects modify base value and end instantly
	if (EffectData.bIsInstant)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, true, false, SourceAbilityComponent,EffectStackCount);
		}
		EndEffect();
		return;
	}

	

	// Duration Effects that aren't periodic alter modifiers, not base
	if (!EffectData.bIsInstant && EffectData.Period == 0)
	{
		EffectData.bNegateEffectAtEnd = true;
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, false, false, SourceAbilityComponent,EffectStackCount);
		}
	}

	// Tick period at start
	if (EffectData.bPeriodTickAtStart && EffectData.Period > 0)
	{
		PeriodTick();
	}
				
	// Instant effects instantly end
	if (EffectData.bIsInstant)
	{
		EndEffect();
	}

	UpdateState(EGMASEffectState::Started, true);
}


void UGMCAbilityEffect::EndEffect()
{
	if (bCompleted) return;
	bCompleted = true;

	// Handle stack expiration policy
	if (StackExpirationPolicy == EAbilityEffectStackingExpirationPolicy::RemoveSingleStackAndRefreshDuration)
	{

		int32 oldStack = EffectStackCount;
		EffectStackCount--;
		
		OnStackCountChange(this, oldStack,EffectStackCount);
		
	
		if (EffectStackCount > 0)
		{
			// Refresh duration and continue
			EffectData.EndTime = OwnerAbilityComponent->ActionTimer + EffectData.Duration;
			bCompleted = false;
			return;
		}
	}

	// Normal cleanup for expired effects
	if (EffectData.bNegateEffectAtEnd)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, false, true,SourceAbilityComponent, EffectStackCount);
		}
	}

	RemoveTagsFromOwner(EffectData.bPreserveGrantedTagsIfMultiple);
	FAbilityEffectRemovalInfo RemovalInfo;
    
	
	RemovalInfo.bPrematureRemoval = true; 
	RemovalInfo.StackCount = EffectStackCount;
	RemovalInfo.EffectContextActor = OwnerAbilityComponent->GetOwner();
	RemovalInfo.ActiveEffect = this;
    
	auto Delegate = OwnerAbilityComponent->OnAbilityEffectRemovedDelegate(EffectData.EffectID);
	if (Delegate != nullptr)
	{
		Delegate->Broadcast(RemovalInfo);
	}
	else
	{
		// Log the failure to find the delegate and handle it appropriately
		UE_LOG(LogTemp, Warning, TEXT("Delegate for EffectID %d not found!"), EffectData.EffectID);
	}


	RemoveAbilitiesFromOwner();
}

void UGMCAbilityEffect::BeginDestroy() {


	// This is addition is mostly to catch ghost effect who are still in around.
	// it's a bug, and ideally should not happen but that happen. a check in engine is added to catch this, and an error log for packaged game.
	/*if (OwnerAbilityComponent) {
		for (TTuple<int, UGMCAbilityEffect*> Effect : OwnerAbilityComponent->GetActiveEffects())
		{
			if (Effect.Value == this) {
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("Effect %s is still in the active effect list of %s"), *Effect.Value->EffectData.EffectTag.ToString(), *OwnerAbilityComponent->GetOwner()->GetName());
				
				if (!bCompleted) {
					UE_LOG(	LogGMCAbilitySystem, Error, TEXT("Effect %s is being destroyed without being completed"), *Effect.Value->EffectData.EffectTag.ToString());
					EndEffect();
				}
				
				Effect.Value = nullptr;
			}
		}
	}*/
	
	UObject::BeginDestroy();
}


void UGMCAbilityEffect::Tick(float DeltaTime)
{
	if (bCompleted) return;
	EffectData.CurrentDuration += DeltaTime;
	TickEvent(DeltaTime);


	
	// Ensure tag requirements are met before applying the effect
	if( ( EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
		DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
	}


	// If there's a period, check to see if it's time to tick
	if (!IsPeriodPaused() && EffectData.Period > 0 && CurrentState == EGMASEffectState::Started)
	{
		const float Mod = FMath::Fmod(OwnerAbilityComponent->ActionTimer, EffectData.Period);
		if (Mod < PrevPeriodMod)
		{
			PeriodTick();
		}
		PrevPeriodMod = Mod;
	}
	
	CheckState();
}

void UGMCAbilityEffect::TickEvent_Implementation(float DeltaTime)
{
}


bool UGMCAbilityEffect::AttributeDynamicCondition_Implementation() const {
	return true;
}


void UGMCAbilityEffect::OnStackCountChange(UGMCAbilityEffect* ActiveEffect, int32 OldStackCount, int32 NewStackCount)
{
	// Check if stack count changed
	if (OldStackCount != NewStackCount)
	{
		// Trigger the delegate with the current data
		if (OwnerAbilityComponent)
		{
			int32 EffectHandle = EffectData.EffectID;
			OwnerAbilityComponent->OnAbilityEffectStackChangeDelegate(EffectHandle)->Broadcast(EffectHandle, NewStackCount, OldStackCount);
		}
	}
}

void UGMCAbilityEffect::SetStackCount(int32 NewStackCount)
{
	EffectStackCount = NewStackCount;
}

int32 UGMCAbilityEffect::GetStackCount() const
{
	return EffectStackCount;
}

int32 UGMCAbilityEffect::GetStackLimitCount() const
{
	return 0;
}

void UGMCAbilityEffect::	PeriodTick()
{
	if (AttributeDynamicCondition()) {
		for (const FGMCAttributeModifier& AttributeModifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(AttributeModifier, true, false, SourceAbilityComponent,EffectStackCount);
		}
	}
}

void UGMCAbilityEffect::UpdateState(EGMASEffectState State, bool Force)
{
	if (State == EGMASEffectState::Ended)
	{
	//	UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Effect Ended"));
	}

	CurrentState = State;
}

bool UGMCAbilityEffect::IsPeriodPaused()
{
	return DoesOwnerHaveTagFromContainer(EffectData.PausePeriodicEffect);
}

void UGMCAbilityEffect::AddTagsToOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedTags)
	{
		OwnerAbilityComponent->AddActiveTag(Tag);
	}
}

void UGMCAbilityEffect::RemoveTagsFromOwner(bool bPreserveOnMultipleInstances)
{
	if (bPreserveOnMultipleInstances)
	{
		if (EffectData.EffectTag.IsValid()) {
			TArray<UGMCAbilityEffect*> ActiveEffect = OwnerAbilityComponent->GetActiveEffectsByTag(EffectData.EffectTag);
			
			if (ActiveEffect.Num() > 1) {
				return;
			}
		}
		else
		{
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Effect Tag is not valid with PreserveMultipleInstances in UGMCAbilityEffect::RemoveTagsFromOwner"));
		}
	}


	
	for (const FGameplayTag Tag : EffectData.GrantedTags)
	{
		OwnerAbilityComponent->RemoveActiveTag(Tag);
	}
}

void UGMCAbilityEffect::AddAbilitiesToOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->GrantAbilityByTag(Tag);
	}
}

void UGMCAbilityEffect::PlayQueue()
{
	AActor* OwningActor =OwnerAbilityComponent->GetOwner();  // Assuming the effect is owned by an actor

	if (EffectData.GameCues.IsEmpty())
	{
		// Log the authority status when GameCues is empty
		if (OwningActor)
		{
			if (OwningActor->HasAuthority()) // Server
			{

				UE_LOG(LogTemp, Log, TEXT("No cues attached. This is being executed on the server."));
				
			}
			else // Client
			{
				UE_LOG(LogTemp, Log, TEXT("No cues attached. This is being executed on a client."));
			}
		}

		UE_LOG(LogTemp, Warning, TEXT("No cues attached"));
		return;
	}


	// Log the owner or player executing the effect

	if (OwningActor)
	{
		UE_LOG(LogTemp, Log, TEXT("Effect played by: %s"), *OwningActor->GetName());
	}

	// Check if the function is being executed on the server or client
	if (OwningActor->HasAuthority()) // If on the server
	{
		UE_LOG(LogTemp, Log, TEXT("This effect is being executed on the server."));
	}
	else // If on a client
	{
		UE_LOG(LogTemp, Log, TEXT("This effect is being executed on a client."));
	}

	// Loop through all the GameCues and invoke the events
	for (const FGameEffectCue Cue : EffectData.GameCues)
	{
		if (OwnerAbilityComponent)
		{
			// Log the GameCue to see which cue is being played
			UE_LOG(LogTemp, Log, TEXT("Executing GameCue: %s"), *Cue.GameCueTags.GetByIndex(0).ToString());
            
			// Invoke the GameCue event
			OwnerAbilityComponent->InvokeGameCueEvent(Cue.GameCueTags.GetByIndex(0), EGameCueEvent::Executed);
		}
	}
}



void UGMCAbilityEffect::RemoveAbilitiesFromOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->RemoveGrantedAbilityByTag(Tag);
	}
}


void UGMCAbilityEffect::EndActiveAbilitiesFromOwner() {
	
	for (const FGameplayTag Tag : EffectData.CancelAbilityOnActivation)
	{
		OwnerAbilityComponent->EndAbilitiesByTag(Tag);
	}
}


bool UGMCAbilityEffect::DoesOwnerHaveTagFromContainer(FGameplayTagContainer& TagContainer) const
{
	for (const FGameplayTag Tag : TagContainer)
	{
		if (OwnerAbilityComponent->HasActiveTag(Tag))
		{
			return true;
		}
	}
	return false;
}

bool UGMCAbilityEffect::DuplicateEffectAlreadyApplied()
{
	if (EffectData.EffectTag == FGameplayTag::EmptyTag)
	{
		return false;
	}
	
	for (const TPair<int, UGMCAbilityEffect*> Effect : OwnerAbilityComponent->GetActiveEffects())
	{
		if (Effect.Value->EffectData.EffectTag == this->EffectData.EffectTag && Effect.Value->bHasStarted)
		{
			return true;
		}
	}

	return false;
}

void UGMCAbilityEffect::CheckState()
{
	switch (CurrentState)
	{
		case EGMASEffectState::Initialized:
			if (OwnerAbilityComponent->ActionTimer >= EffectData.StartTime)
			{
				StartEffect();
				UpdateState(EGMASEffectState::Started, true);
			}
			break;
		case EGMASEffectState::Started:
			if (EffectData.Duration != 0 && OwnerAbilityComponent->ActionTimer >= EffectData.EndTime)
			{
				EndEffect();
			}
			break;
		case EGMASEffectState::Ended:
			break;
	default: break;
	}
}
