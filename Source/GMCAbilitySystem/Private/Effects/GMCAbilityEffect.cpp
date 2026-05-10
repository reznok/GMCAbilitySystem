// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"
#include "Interfaces/IPluginManager.h"
#include "Kismet/KismetSystemLibrary.h"

#if WITH_EDITOR
void UGMCAbilityEffect::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	
	
}
#endif

void UGMCAbilityEffect::InitializeEffect(FGMCAbilityEffectData InitializationData)
{
	EffectData = InitializationData;
	
	OwnerAbilityComponent = EffectData.OwnerAbilityComponent;
	
	if (OwnerAbilityComponent == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("OwnerAbilityComponent is null in UGMCAbilityEffect::InitializeEffect"));
		return;
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


void UGMCAbilityEffect::StartEffect()
{
	bHasStarted = true;

	// Ensure tag requirements are met before applying the effect
	if( ( EffectData.ApplicationMustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustNotHaveTags) ||
	( EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
		return;
	}
	
	// Effect Query
	if (!EffectData.ActivationQuery.IsEmpty() && !EffectData.ActivationQuery.Matches(OwnerAbilityComponent->GetActiveTags()))
		{
		EndEffect();
		return;
	}

	AddTagsToOwner();
	AddAbilitiesToOwner();
	EndActiveAbilitiesFromOwner(EffectData.CancelAbilityOnActivation);

	EndActiveAbilitiesByDefinitionQuery(EffectData.EndAbilityOnActivationQuery);

	bHasAppliedEffect = true;

	OwnerAbilityComponent->OnEffectApplied.Broadcast(this);

	// Instant effects modify base value and end instantly
	if (EffectData.EffectType == EGMASEffectType::Instant
		|| EffectData.EffectType == EGMASEffectType::Persistent
		|| (EffectData.EffectType == EGMASEffectType::Periodic && EffectData.bPeriodicFirstTick))
	{
		for (int i = 0; i < EffectData.Modifiers.Num(); i++)
		{
			FGMCAttributeModifier ModCpy = EffectData.Modifiers[i];
			ModCpy.InitModifier(this, OwnerAbilityComponent->ActionTimer, i, IsEffectModifiersRegisterInHistory(), 1.f);
			OwnerAbilityComponent->ApplyAbilityAttributeModifier(ModCpy);
			OnAttributeModifierApplication(ModCpy);
		}

		if (EffectData.EffectType == EGMASEffectType::Instant)
		{
			EndEffect();
		}
	}
	
	StartEffectEvent();

	UpdateState(EGMASEffectState::Started, true);
}


void UGMCAbilityEffect::OnAttributeModifierApplication(const FGMCAttributeModifier& Modifier)
{
	if (bCallOnAttributeModifierApplication)
	{
		K2_OnAttributeModifierApplication(Modifier);
	}
}

void UGMCAbilityEffect::EndEffect()
{

	// Prevent EndEffect from being called multiple times
	if (bCompleted) return;

	bCompleted = true;
	if (CurrentState != EGMASEffectState::Ended)
	{
		UpdateState(EGMASEffectState::Ended, true);
	}
	

	// Only remove tags and abilities if the effect has started and applied
	if (!bHasStarted || !bHasAppliedEffect) return;
		// If the effect is not an instant effect, we need to negate the modifiers
	if (IsEffectModifiersRegisterInHistory())
	{
		for (int i = 0; i < EffectData.Modifiers.Num(); i++)
		{
			if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(EffectData.Modifiers[i].AttributeTag))
			{
				Attribute->RemoveTemporalModifier(i, this);
			}
		}
	}
	
	EndActiveAbilitiesByDefinitionQuery(EffectData.EndAbilityOnEndQuery);

	EndActiveAbilitiesFromOwner(EffectData.CancelAbilityOnEnd);
	RemoveTagsFromOwner(EffectData.bPreserveGrantedTagsIfMultiple);
	RemoveAbilitiesFromOwner();

	OwnerAbilityComponent->OnEffectRemoved.Broadcast(this);

	EndEffectEvent();

	// Chain hooks: apply / remove effects when this one ends. Same queue-type detection as
	// FinishEndAbility — Predicted requires being inside a GMC tick or Standalone, otherwise
	// PredictedQueued is used to defer until the next safe window.
	if (OwnerAbilityComponent && (EffectData.ApplyEffectOnEnd.Num() > 0 || !EffectData.RemoveEffectOnEnd.IsEmpty()))
	{
		const bool bInsideGMCTick =
			(OwnerAbilityComponent->GMCMovementComponent && OwnerAbilityComponent->GMCMovementComponent->IsExecutingMove())
			|| OwnerAbilityComponent->IsInAncillaryTick()
			|| OwnerAbilityComponent->GetNetMode() == NM_Standalone;
		const EGMCAbilityEffectQueueType ChainQueueType =
			bInsideGMCTick ? EGMCAbilityEffectQueueType::Predicted : EGMCAbilityEffectQueueType::PredictedQueued;

		for (const TSubclassOf<UGMCAbilityEffect>& EffectClass : EffectData.ApplyEffectOnEnd)
		{
			if (EffectClass)
			{
				OwnerAbilityComponent->ApplyAbilityEffectShort(EffectClass, ChainQueueType);
			}
		}

		for (const FGameplayTag& EffectTag : EffectData.RemoveEffectOnEnd)
		{
			if (EffectTag.IsValid())
			{
				OwnerAbilityComponent->RemoveEffectByTagSafe(EffectTag, -1, ChainQueueType);
			}
		}
	}
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
	// Consume the bilateral predicted-end defer. Uses an absolute ActionTimer timestamp instead of a
	// per-tick countdown — both client and server compute the same EndAtActionTimer (same move log,
	// same ActionTimer at Remove + same ClientGraceTime), and the comparison below fires on the
	// exact same logical tick on both sides regardless of DeltaTime / framerate / replay count.
	if (EndAtActionTimer >= 0.0 && OwnerAbilityComponent
		&& OwnerAbilityComponent->ActionTimer >= EndAtActionTimer)
	{
		EndAtActionTimer = -1.0;
		EndEffect();
		return;
	}

	if (bCompleted) {
		return;
	}

	// Suspended pending the server's verdict on a successor effect. Stops applying
	// modifiers; keeps everything else (tags, abilities, EndAtActionTimer) so the
	// component can revive us if the successor is rejected. Note we still let the
	// natural EndAtActionTimer path above fire — bilateral defer expiration trumps
	// the replacement protocol (the successor revival, if it would have happened,
	// is moot once we've ended naturally).
	if (bPendingDeathBySuccessor) {
		return;
	}

	EffectData.CurrentDuration = OwnerAbilityComponent->ActionTimer - EffectData.StartTime;
	TickEvent(DeltaTime);
	
	// Ensure tag requirements are met before applying the effect
	if( (EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
		DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
	}

	// query to maintain effect — end the effect when the query is no longer satisfied
	if ( !EffectData.MustMaintainQuery.IsEmpty() && !EffectData.MustMaintainQuery.Matches(OwnerAbilityComponent->GetActiveTags()))
	{
		EndEffect();
	}

	
	if (!IsPaused() && CurrentState == EGMASEffectState::Started && AttributeDynamicCondition())
	{
		if (EffectData.EffectType == EGMASEffectType::Ticking) {
		// If there's a period, check to see if it's time to tick

			for (int i = 0; i < EffectData.Modifiers.Num(); i++) {
				FGMCAttributeModifier Modifier = EffectData.Modifiers[i];
				Modifier.InitModifier(this, OwnerAbilityComponent->ActionTimer, i, IsEffectModifiersRegisterInHistory(), DeltaTime);
				OwnerAbilityComponent->ApplyAbilityAttributeModifier(Modifier);
				OnAttributeModifierApplication(Modifier);
			} // End for each modifier

			
		} // End Ticking
		else if (EffectData.EffectType == EGMASEffectType::Periodic)
		{
			
			const float CurrentElapsedTime = OwnerAbilityComponent->ActionTimer -  EffectData.StartTime;
			float PreviousElapsedTime = CurrentElapsedTime - DeltaTime;
			PreviousElapsedTime = FMath::Max(PreviousElapsedTime, 0.f); // Ensure we don't go negative

			int32 PreviousPeriod = FMath::TruncToInt(PreviousElapsedTime / EffectData.PeriodicInterval);
			int32 CurrentPeriod =	FMath::TruncToInt(CurrentElapsedTime / EffectData.PeriodicInterval);
			
			if (CurrentPeriod > PreviousPeriod) {
				int32 NumTickToApply = CurrentPeriod - PreviousPeriod;
				
				for (int i = 0; i < NumTickToApply; i++) {
					for (int y = 0; y < EffectData.Modifiers.Num(); y++) {
						FGMCAttributeModifier Modifier = EffectData.Modifiers[y];
						Modifier.InitModifier(this, OwnerAbilityComponent->ActionTimer, y, IsEffectModifiersRegisterInHistory(), 1.f);
						OwnerAbilityComponent->ApplyAbilityAttributeModifier(Modifier);
						OnAttributeModifierApplication(Modifier);
					}
				}

				if (NumTickToApply > 0)
				{
					PeriodTick();
				}
			}
			
		}
	}

	
	
	
	CheckState();
}

int32 UGMCAbilityEffect::CalculatePeriodicTicksBetween(float Period, float StartActionTimer, float EndActionTimer)
{
	if (Period <= 0.0f || EndActionTimer <= StartActionTimer) { return 0; }
	
	float FirstTick = FMath::CeilToFloat(StartActionTimer / Period) * Period;
	if (FirstTick > EndActionTimer) { return 0; }


	float LastTick = FMath::FloorToFloat(EndActionTimer / Period) * Period;
	
	return FMath::RoundToInt((LastTick - FirstTick) / Period) + 1;
}

void UGMCAbilityEffect::TickEvent_Implementation(float DeltaTime)
{
}


bool UGMCAbilityEffect::AttributeDynamicCondition_Implementation() const {
	return true;
}


void UGMCAbilityEffect::PeriodTick()
{
	PeriodTickEvent();
}

void UGMCAbilityEffect::PeriodTickEvent_Implementation()
{
}

void UGMCAbilityEffect::UpdateState(EGMASEffectState State, bool Force)
{
	if (State == EGMASEffectState::Ended)
	{
	//	UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Effect Ended"));
	}

	CurrentState = State;
}

bool UGMCAbilityEffect::IsPaused()
{
	return DoesOwnerHaveTagFromContainer(EffectData.PauseEffect);
}

bool UGMCAbilityEffect::IsEffectModifiersRegisterInHistory() const
{
	return EffectData.EffectType != EGMASEffectType::Instant && EffectData.bNegateEffectAtEnd;
}

float UGMCAbilityEffect::ProcessCustomModifier(const TSubclassOf<UGMCAttributeModifierCustom_Base>& MCClass, const FAttribute* Attribute)
{
	UGMCAttributeModifierCustom_Base** MCI = CustomModifiersInstances.Find(MCClass);
	if (MCI == nullptr)
	{
		MCI = &CustomModifiersInstances.Add(MCClass, NewObject<UGMCAttributeModifierCustom_Base>(this, MCClass));
	}

	if (*MCI == nullptr)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Custom Modifier Instance is null for class %s in UGMCAbilityEffect::ProcessCustomModifier"), *MCClass->GetName());
		return 0.f;
	}

	return (*MCI)->Calculate(this, Attribute);
}


void UGMCAbilityEffect::GetOwnerActor(AActor*& OutOwnerActor) const
{
	if (OwnerAbilityComponent)
	{
		OutOwnerActor = OwnerAbilityComponent->GetOwner();
	}
	else
	{
		OutOwnerActor = nullptr;
	}
}

AActor* UGMCAbilityEffect::GetOwnerActor() const
{
	return OwnerAbilityComponent ? OwnerAbilityComponent->GetOwner() : nullptr;
}

void UGMCAbilityEffect::AddTagsToOwner()
{
	// Route ClientAuth-applied tags to the non-bound container so they don't trigger
	// GMC state-divergence detection. See plan_gmas_client_auth_tags_routing.md.
	for (const FGameplayTag Tag : EffectData.GrantedTags)
	{
		if (EffectData.bClientAuth)
		{
			OwnerAbilityComponent->AddClientAuthActiveTag(Tag);
		}
		else
		{
			OwnerAbilityComponent->AddActiveTag(Tag);
		}
	}
}

void UGMCAbilityEffect::RemoveTagsFromOwner(bool bPreserveOnMultipleInstances)
{
	if (bPreserveOnMultipleInstances)
	{
		if (EffectData.EffectTag.IsValid()) {
			// Skip self and bCompleted zombies: ActiveEffects retains entries until the
			// next TickActiveEffects cleanup pass, and counting them as siblings would
			// preserve tags that no live instance owns. Deferred (EndAtActionTimer >= 0,
			// !bCompleted) DOES count — still ticking modifiers, still owns the slot.
			const TArray<UGMCAbilityEffect*> SameTagEffects =
				OwnerAbilityComponent->GetActiveEffectsByTag(EffectData.EffectTag);

			int32 OthersStillAlive = 0;
			for (const UGMCAbilityEffect* Other : SameTagEffects)
			{
				if (Other && Other != this && !Other->bCompleted)
				{
					++OthersStillAlive;
				}
			}

			if (OthersStillAlive > 0) {
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
		if (EffectData.bClientAuth)
		{
			OwnerAbilityComponent->RemoveClientAuthActiveTag(Tag);
		}
		else
		{
			OwnerAbilityComponent->RemoveActiveTag(Tag);
		}
	}
}

void UGMCAbilityEffect::AddAbilitiesToOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->GrantAbilityByTag(Tag);
	}
}

void UGMCAbilityEffect::RemoveAbilitiesFromOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->RemoveGrantedAbilityByTag(Tag);
	}
}


void UGMCAbilityEffect::EndActiveAbilitiesFromOwner(const FGameplayTagContainer& TagContainer) {
	
	for (const FGameplayTag Tag : TagContainer)
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

void UGMCAbilityEffect::EndActiveAbilitiesByDefinitionQuery(FGameplayTagQuery EndAbilityOnActivationViaDefinitionQuery)
{

	if (EndAbilityOnActivationViaDefinitionQuery.IsEmpty()) return;

	int NumCancelled = OwnerAbilityComponent->EndAbilitiesByQuery(EndAbilityOnActivationViaDefinitionQuery);

	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Effect %s cancelled %d ability(ies) via EffectDefinition query."),
		*EffectData.EffectTag.ToString(), NumCancelled);
}

void UGMCAbilityEffect::ModifyMustMaintainQuery(const FGameplayTagQuery& NewQuery)
{
	EffectData.MustMaintainQuery = NewQuery;
	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("MustMainQuery modified: %s"), *NewQuery.GetDescription());
}

void UGMCAbilityEffect::ModifyEndAbilitiesOnEndQuery(const FGameplayTagQuery& NewQuery)
{
	EffectData.EndAbilityOnEndQuery = NewQuery;
	UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("EndAbilityOnEndViaDefinitionQuery modified: %s"), *NewQuery.GetDescription());
}
