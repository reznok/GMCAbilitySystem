// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"
#include "Interfaces/IPluginManager.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Kismet/KismetSystemLibrary.h"

#if WITH_EDITOR
void UGMCAbilityEffect::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	UObject::PostEditChangeProperty(PropertyChangedEvent);
	
	
}
#endif

FGMCAbilityEffectData UGMCAbilityEffect::GetDefaultEffectData(TSubclassOf<UGMCAbilityEffect> EffectClass)
{
	if (!EffectClass)
	{
		return FGMCAbilityEffectData{};
	}
	const UGMCAbilityEffect* CDO = EffectClass.GetDefaultObject();
	return CDO ? CDO->EffectData : FGMCAbilityEffectData{};
}

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
	OwnerAbilityComponent->StampReplayTransition(this, ReplayCreatedStamp);

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
	// Stamped before the modifiers: an Instant effect ends inside the loop below, and its end must sort after its start.
	OwnerAbilityComponent->StampReplayTransition(this, ReplayStartStamp);

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
			if (!ModCpy.ResolveConditions(OwnerAbilityComponent)) continue; // conditional skip / override
			OwnerAbilityComponent->ApplyAbilityAttributeModifier(ModCpy);
			OnAttributeModifierApplication(ModCpy);
		}

		if (EffectData.EffectType == EGMASEffectType::Instant)
		{
			EndEffect();
		}
	}

	if (IsEffectModifiersRegisterInHistory() && OwnerAbilityComponent->IsReplayJournalActive())
	{
		CaptureTemporalModifiers(ReplayStartTemporalModifiers);
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
	// A pending arm has nothing left to end.
	PendingEndGrace = -1.0;
	if (CurrentState != EGMASEffectState::Ended)
	{
		UpdateState(EGMASEffectState::Ended, true);
	}
	

	// Only remove tags and abilities if the effect has started and applied
	if (!bHasStarted || !bHasAppliedEffect) return;

	OwnerAbilityComponent->StampReplayTransition(this, ReplayEndStamp);
	if (IsEffectModifiersRegisterInHistory() && OwnerAbilityComponent->IsReplayJournalActive())
	{
		// Before the removal below: a replay that starts inside this effect's lifetime puts these entries back.
		CaptureTemporalModifiers(ReplayEndTemporalModifiers);
	}

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
	// Per-effect profiling scope, named by effect tag (class-name fallback). Zero cost in
	// Shipping (macro + argument compiled out when CPUPROFILERTRACE_ENABLED == 0).
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*FString::Printf(TEXT("Effect::Tick [%s]"),
		EffectData.EffectTag.IsValid() ? *EffectData.EffectTag.ToString() : *GetClass()->GetName()));

	// Consume the bilateral predicted-end defer. Uses an absolute ActionTimer timestamp instead of a
	// per-tick countdown — both client and server compute the same EndAtActionTimer (same move log,
	// same ActionTimer at Remove + same ClientGraceTime), and the comparison below fires on the
	// exact same logical tick on both sides regardless of DeltaTime / framerate / replay count.
	// Strict: an effect removed during move T still ticks in move T, whatever the phase of the Remove call.
	if (PendingEndGrace >= 0.0 && OwnerAbilityComponent && !OwnerAbilityComponent->IsReplayingForGMASLogic())
	{
		// A replayed move is older than the Remove: only a local move arms it.
		const double Grace = PendingEndGrace;
		PendingEndGrace = -1.0;
		OwnerAbilityComponent->ArmEffectEnd(this, Grace);
	}
	if (EndAtActionTimer >= 0.0 && OwnerAbilityComponent
		&& OwnerAbilityComponent->ActionTimer > EndAtActionTimer)
	{
		EndAtActionTimer = -1.0;
		EndEffect();
		return;
	}

	if (bCompleted) {
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
		ApplyTickModifiers(DeltaTime, true);
	}
	else if (CurrentState == EGMASEffectState::Started
		&& EffectData.EffectType == EGMASEffectType::Periodic
		&& (IsPaused() || !AttributeDynamicCondition()))
	{
		// Period boundaries crossed while paused (or while the dynamic condition is false)
		// are DROPPED, not deferred — the schedule stays anchored on StartTime, so the next
		// application happens at the next absolute boundary after resume. This is a design
		// constraint, not an oversight: deferring would need a paused-time accumulator,
		// which cannot be made replay-safe (see the stateless-detection comment above).
		// Log the drops so balance-affecting pauses are visible instead of silent.
		const float CurrentElapsedTime = OwnerAbilityComponent->ActionTimer - EffectData.StartTime;
		const float PreviousElapsedTime = FMath::Max(CurrentElapsedTime - DeltaTime, 0.f);
		const int32 SkippedTicks = FMath::TruncToInt(CurrentElapsedTime / EffectData.PeriodicInterval)
			- FMath::TruncToInt(PreviousElapsedTime / EffectData.PeriodicInterval);
		if (SkippedTicks > 0)
		{
			UE_LOG(LogGMCAbilitySystem, Verbose,
				TEXT("Periodic effect %s dropped %d tick(s) while %s (by design — boundaries are not deferred)."),
				*GetName(), SkippedTicks, IsPaused() ? TEXT("paused") : TEXT("dynamic condition false"));
		}
	}

	
	
	
	CheckState();
}

void UGMCAbilityEffect::ApplyTickModifiers(float DeltaTime, bool bLiveTick)
{
	if (EffectData.EffectType == EGMASEffectType::Ticking) {
	// If there's a period, check to see if it's time to tick

		for (int i = 0; i < EffectData.Modifiers.Num(); i++) {
			FGMCAttributeModifier Modifier = EffectData.Modifiers[i];
			Modifier.InitModifier(this, OwnerAbilityComponent->ActionTimer, i, IsEffectModifiersRegisterInHistory(), DeltaTime);
			if (!Modifier.ResolveConditions(OwnerAbilityComponent)) continue; // conditional skip / override
			OwnerAbilityComponent->ApplyAbilityAttributeModifier(Modifier);
			if (bLiveTick) { OnAttributeModifierApplication(Modifier); }
		} // End for each modifier

		
	} // End Ticking
	else if (EffectData.EffectType == EGMASEffectType::Persistent
		&& EffectData.bReevaluateConditionsWhilePersistent
		&& IsEffectModifiersRegisterInHistory())
	{
		// Re-evaluate each modifier's Conditions every tick: drop this index's temporal entry
		// from the previous tick, then re-resolve. Keeps a SINGLE maintained entry — a constant
		// buff/debuff that toggles on/off (or swaps its value source) with a tag, WITHOUT
		// accumulating like a Ticking effect. Gated on IsEffectModifiersRegisterInHistory() so we
		// only ever touch temporal modifiers, never bake into RawValue. The temporal list is
		// local + non-replicated, so this churn never hits the wire — only the resolved Value does.
		for (int i = 0; i < EffectData.Modifiers.Num(); i++)
		{
			if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(EffectData.Modifiers[i].AttributeTag))
			{
				Attribute->RemoveTemporalModifier(i, this);
			}
			FGMCAttributeModifier Modifier = EffectData.Modifiers[i];
			Modifier.InitModifier(this, OwnerAbilityComponent->ActionTimer, i, IsEffectModifiersRegisterInHistory(), 1.f);
			if (!Modifier.ResolveConditions(OwnerAbilityComponent)) continue; // skip -> contribution stays removed this tick
			OwnerAbilityComponent->ApplyAbilityAttributeModifier(Modifier);
			if (bLiveTick) { OnAttributeModifierApplication(Modifier); }
		}
	}
	else if (EffectData.EffectType == EGMASEffectType::Periodic)
	{
		// STATELESS period detection on purpose: the crossing test is a pure function of
		// (ActionTimer, StartTime, DeltaTime). Per-move windows tile exactly (move N's end
		// is move N+1's start), so boundaries are counted once and exactly once — and a
		// client replay re-executes the same moves with the same stored DeltaTimes, so it
		// recomputes the SAME boundary crossings (bound attributes were rolled back, so
		// re-applying is the prediction model working as intended). Do NOT introduce
		// per-effect accumulators here (e.g. pause-time shifting): they would mutate
		// during replayed ticks without being rolled back and desync client from server.
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
					if (!Modifier.ResolveConditions(OwnerAbilityComponent)) continue; // conditional skip / override
					OwnerAbilityComponent->ApplyAbilityAttributeModifier(Modifier);
					if (bLiveTick) { OnAttributeModifierApplication(Modifier); }
				}
			}

			if (NumTickToApply > 0)
			{
				if (bLiveTick) { PeriodTick(); }
			}
		}

	}

	// The replay of an ended effect re-applies its tick modifiers up to this move, not beyond.
	const bool bTickWritesModifiers = EffectData.EffectType == EGMASEffectType::Ticking
		|| EffectData.EffectType == EGMASEffectType::Periodic
		|| (EffectData.EffectType == EGMASEffectType::Persistent && EffectData.bReevaluateConditionsWhilePersistent && IsEffectModifiersRegisterInHistory());
	if (bLiveTick && bTickWritesModifiers)
	{
		ReplayLastModifierTickTime = OwnerAbilityComponent->ActionTimer;
		bHasReplayLastModifierTick = true;
	}
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
	ReplayAddedTags.Reset();
	for (const FGameplayTag Tag : EffectData.GrantedTags)
	{
		if (EffectData.bClientAuth)
		{
			OwnerAbilityComponent->AddClientAuthActiveTag(Tag);
		}
		else
		{
			OwnerAbilityComponent->AddActiveTag(Tag);
			ReplayAddedTags.Add(Tag);
		}
	}
}

void UGMCAbilityEffect::RemoveTagsFromOwner(bool bPreserveOnMultipleInstances)
{
	ReplayRemovedTags.Reset();
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
		else if (!EffectData.GrantedTags.IsEmpty())
		{
			// Only an effect that grants tags has something to preserve: the default flag is true on every effect.
			UE_LOG(LogGMCAbilitySystem, Warning,
				TEXT("Effect Tag is not valid with PreserveMultipleInstances in UGMCAbilityEffect::RemoveTagsFromOwner (%s: give it an EffectTag, or this instance removes the tags of the others)"),
				*GetClass()->GetName());
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
			ReplayRemovedTags.Add(Tag);
		}
	}
}

void UGMCAbilityEffect::AddAbilitiesToOwner()
{
	ReplayGrantedAbilityTags.Reset();
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->GrantAbilityByTag(Tag);
		ReplayGrantedAbilityTags.Add(Tag);
	}
}

void UGMCAbilityEffect::RemoveAbilitiesFromOwner()
{
	ReplayRemovedAbilityTags.Reset();
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->RemoveGrantedAbilityByTag(Tag);
		ReplayRemovedAbilityTags.Add(Tag);
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

void UGMCAbilityEffect::EndEffectEvent_Implementation()
{
}

void UGMCAbilityEffect::StartEffectEvent_Implementation()
{
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

bool UGMCAbilityEffect::IsReplayedBeforeStart(double MoveTime, EGMASReplayPhase Phase) const
{
	// A pending stamp resolves on the next local move, so it is later than every replayed move.
	if (ReplayCreatedStamp.bPending || (bHasAppliedEffect && ReplayStartStamp.bPending))
	{
		return true;
	}
	if (ReplayCreatedStamp.bSet && !ReplayCreatedStamp.IsAtOrBefore(MoveTime, Phase))
	{
		return true;
	}
	return bHasAppliedEffect && ReplayStartStamp.bSet && !ReplayStartStamp.IsAtOrBefore(MoveTime, Phase);
}

void UGMCAbilityEffect::ReplayStartWrites()
{
	if (!OwnerAbilityComponent) return;

	for (const FGameplayTag& Tag : ReplayAddedTags)
	{
		OwnerAbilityComponent->AddActiveTag(Tag);
	}
	for (const FGameplayTag& Tag : ReplayGrantedAbilityTags)
	{
		OwnerAbilityComponent->GrantAbilityByTag(Tag);
	}

	const bool bAppliesModifiersAtStart = EffectData.EffectType == EGMASEffectType::Instant
		|| EffectData.EffectType == EGMASEffectType::Persistent
		|| (EffectData.EffectType == EGMASEffectType::Periodic && EffectData.bPeriodicFirstTick);
	if (!bAppliesModifiersAtStart) return;

	if (IsEffectModifiersRegisterInHistory())
	{
		RestoreTemporalModifiers(ReplayStartTemporalModifiers, TNumericLimits<double>::Max(), true);
		return;
	}

	// Permanent path: RawValue is bound and was rewound, so the start application runs again.
	for (int i = 0; i < EffectData.Modifiers.Num(); i++)
	{
		FGMCAttributeModifier ModCpy = EffectData.Modifiers[i];
		ModCpy.InitModifier(this, OwnerAbilityComponent->ActionTimer, i, false, 1.f);
		if (!ModCpy.ResolveConditions(OwnerAbilityComponent)) continue;
		OwnerAbilityComponent->ApplyAbilityAttributeModifier(ModCpy);
	}
}

void UGMCAbilityEffect::ReplayEndWrites()
{
	if (!OwnerAbilityComponent) return;

	if (IsEffectModifiersRegisterInHistory())
	{
		for (int i = 0; i < EffectData.Modifiers.Num(); i++)
		{
			if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(EffectData.Modifiers[i].AttributeTag))
			{
				Attribute->RemoveTemporalModifier(i, this);
				// The live end made the attribute dirty. Mirror it even when no entry was put back.
				Attribute->MarkDirty();
			}
		}
	}

	for (const FGameplayTag& Tag : ReplayRemovedTags)
	{
		OwnerAbilityComponent->RemoveActiveTag(Tag);
	}
	for (const FGameplayTag& Tag : ReplayRemovedAbilityTags)
	{
		OwnerAbilityComponent->RemoveGrantedAbilityByTag(Tag);
	}
}

void UGMCAbilityEffect::ReplayGhostTick(float DeltaTime)
{
	if (!OwnerAbilityComponent || IsPaused() || !AttributeDynamicCondition()) return;
	ApplyTickModifiers(DeltaTime, false);
}

void UGMCAbilityEffect::RestoreGhostTemporalModifiersUpTo(double MaxActionTimer)
{
	RestoreTemporalModifiers(ReplayEndTemporalModifiers, MaxActionTimer, true);
}

void UGMCAbilityEffect::RemoveAllTemporalModifiers()
{
	if (!OwnerAbilityComponent) return;

	for (int i = 0; i < EffectData.Modifiers.Num(); i++)
	{
		if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(EffectData.Modifiers[i].AttributeTag))
		{
			Attribute->RemoveTemporalModifier(i, this);
		}
	}
}

void UGMCAbilityEffect::RestampStartTemporalModifiers(double NewTime)
{
	if (!OwnerAbilityComponent || ReplayStartTemporalModifiers.IsEmpty()) return;

	for (FGMASReplayTemporalEntry& Entry : ReplayStartTemporalModifiers)
	{
		if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Entry.AttributeTag))
		{
			Attribute->RemoveTemporalModifier(Entry.ApplicationIndex, this);
		}
		Entry.ActionTimer = NewTime;
	}
	RestoreTemporalModifiers(ReplayStartTemporalModifiers, TNumericLimits<double>::Max(), true);
}

void UGMCAbilityEffect::CaptureTemporalModifiers(TArray<FGMASReplayTemporalEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!OwnerAbilityComponent) return;

	TArray<FGameplayTag, TInlineAllocator<4>> VisitedAttributes;
	for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
	{
		if (VisitedAttributes.Contains(Modifier.AttributeTag)) continue;
		VisitedAttributes.Add(Modifier.AttributeTag);

		const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Modifier.AttributeTag);
		if (!Attribute) continue;

		TArray<FAttributeTemporaryModifier> Entries;
		Attribute->GetTemporalModifiersOf(this, Entries);
		for (const FAttributeTemporaryModifier& Entry : Entries)
		{
			FGMASReplayTemporalEntry& Out = OutEntries.AddDefaulted_GetRef();
			Out.AttributeTag = Modifier.AttributeTag;
			Out.ApplicationIndex = Entry.ApplicationIndex;
			Out.Value = Entry.Value;
			Out.ActionTimer = Entry.ActionTimer;
			Out.Kind = static_cast<uint8>(Entry.Kind);
		}
	}
}

void UGMCAbilityEffect::RestoreTemporalModifiers(const TArray<FGMASReplayTemporalEntry>& Entries, double MaxActionTimer, bool bInclusive) const
{
	if (!OwnerAbilityComponent) return;

	for (const FGMASReplayTemporalEntry& Entry : Entries)
	{
		const bool bInRange = bInclusive ? Entry.ActionTimer <= MaxActionTimer : Entry.ActionTimer < MaxActionTimer;
		if (!bInRange) continue;

		if (const FAttribute* Attribute = OwnerAbilityComponent->GetAttributeByTag(Entry.AttributeTag))
		{
			FAttributeTemporaryModifier Modifier;
			Modifier.ApplicationIndex = Entry.ApplicationIndex;
			Modifier.Value = Entry.Value;
			Modifier.ActionTimer = Entry.ActionTimer;
			Modifier.InstigatorEffect = const_cast<UGMCAbilityEffect*>(this);
			Modifier.Kind = static_cast<EAttributeModifierKind>(Entry.Kind);
			Attribute->RestoreTemporalModifier(Modifier);
		}
	}
}
