// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"

float FGMCAbilityEffectData::GetDelay() const
{
	// Return value from attribute if DelayAttributeTag is valid
	if (DelayAttributeTag.IsValid())
	{
		return OwnerAbilityComponent->GetAttributeValueByTag(DelayAttributeTag);
	}

	// Otherwise, fall back to the hard-coded value
	return Delay;
}

float FGMCAbilityEffectData::GetDuration() const
{
	// Return value from attribute if DurationAttributeTag is valid
	if (DurationAttributeTag.IsValid())
	{
		return OwnerAbilityComponent->GetAttributeValueByTag(DurationAttributeTag);
	}

	// Otherwise, fall back to the hard-coded value
	return Duration;
}

float FGMCAbilityEffectData::GetPeriod() const
{
	// Return value from attribute if PeriodAttributeTag is valid
	if (PeriodAttributeTag.IsValid())
	{
		return OwnerAbilityComponent->GetAttributeValueByTag(PeriodAttributeTag);
	}

	// Otherwise, fall back to the hard-coded value
	return Period;
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
	
	ClientEffectApplicationTime = OwnerAbilityComponent->ActionTimer;

	// If server sends times, use those
	// Only used in the case of a non predicted effect
	if (InitializationData.StartTime != 0)
	{
		EffectData.StartTime = InitializationData.StartTime;
	}
	else
	{
		EffectData.StartTime = OwnerAbilityComponent->ActionTimer + EffectData.GetDelay();
	}
	
	if (InitializationData.EndTime != 0)
	{
		EffectData.EndTime = InitializationData.EndTime;
	}
	else
	{
		EffectData.EndTime = EffectData.StartTime + EffectData.GetDuration();
	}

	// Start Immediately
	if (EffectData.GetDelay() == 0)
	{
		StartEffect();
	}
}


void UGMCAbilityEffect::StartEffect()
{
	// Ensure tag requirements are met before applying the effect
	if( ( EffectData.ApplicationMustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.ApplicationMustNotHaveTags) ||
	( EffectData.OngoingMustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.OngoingMustHaveTags) ) ||
	DoesOwnerHaveTagFromContainer(EffectData.OngoingMustNotHaveTags) )
	{
		EndEffect();
		return;
	}

	bHasStarted = true;
	
	AddTagsToOwner();
	AddAbilitiesToOwner();

	// Instant effects modify base value and end instantly
	if (EffectData.bIsInstant)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, true);
		}
		EndEffect();
		return;
	}

	// Duration Effects that aren't periodic alter modifiers, not base
	if (!EffectData.bIsInstant && EffectData.GetPeriod() == 0)
	{
		EffectData.bNegateEffectAtEnd = true;
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, false);
		}
	}

	// Tick period at start
	if (EffectData.bPeriodTickAtStart && EffectData.GetPeriod() > 0)
	{
		PeriodTick();
	}
				
	// Instant effects instantly end
	if (EffectData.bIsInstant)
	{
		EndEffect();
	}

	UpdateState(EEffectState::Started, true);
}

void UGMCAbilityEffect::EndEffect()
{
	// Prevent EndEffect from being called multiple times
	if (bCompleted) return;
	
	bCompleted = true;
	if (CurrentState != EEffectState::Ended)
	{
		UpdateState(EEffectState::Ended, true);
	}

	// Only remove tags and abilities if the effect has started
	if (!bHasStarted) return;

	if (EffectData.bNegateEffectAtEnd)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, false, true);
		}
	}
	
	RemoveTagsFromOwner();
	RemoveAbilitiesFromOwner();
}

void UGMCAbilityEffect::Tick(float DeltaTime)
{
	if (bCompleted) return;
	TickEvent(DeltaTime);
	
	// Ensure tag requirements are met before applying the effect
	if( ( EffectData.OngoingMustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.OngoingMustHaveTags) ) ||
		DoesOwnerHaveTagFromContainer(EffectData.OngoingMustNotHaveTags) )
	{
		EndEffect();
	}


	// If there's a period, check to see if it's time to tick
	if (!IsPeriodPaused() && EffectData.GetPeriod() > 0 && CurrentState == EEffectState::Started)
	{
		const float Mod = FMath::Fmod(OwnerAbilityComponent->ActionTimer, EffectData.GetPeriod());
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

void UGMCAbilityEffect::PeriodTick()
{
	for (const FGMCAttributeModifier& AttributeModifier : EffectData.Modifiers)
	{
		OwnerAbilityComponent->ApplyAbilityEffectModifier(AttributeModifier, true);
	}
}

void UGMCAbilityEffect::UpdateState(EEffectState State, bool Force)
{
	if (State == EEffectState::Ended)
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

void UGMCAbilityEffect::RemoveTagsFromOwner()
{
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

void UGMCAbilityEffect::RemoveAbilitiesFromOwner()
{
	for (const FGameplayTag Tag : EffectData.GrantedAbilities)
	{
		OwnerAbilityComponent->RemoveGrantedAbilityByTag(Tag);
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
		case EEffectState::Initialized:
			if (OwnerAbilityComponent->ActionTimer >= EffectData.StartTime)
			{
				StartEffect();
				UpdateState(EEffectState::Started, true);
			}
			break;
		case EEffectState::Started:
			if (EffectData.GetDuration() != 0 && OwnerAbilityComponent->ActionTimer >= EffectData.EndTime)
			{
				EndEffect();
			}
			break;
		case EEffectState::Ended:
			break;
	default: break;
	}
}
