﻿// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"


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
	if( ( EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
		DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
		return;
	}
	
	AddTagsToOwner();
	AddAbilitiesToOwner();

	// Duration and Instant applies immediately.
	if (EffectData.Period == 0)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier);
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

	if (EffectData.bNegateEffectAtEnd && !EffectData.bIsInstant)
	{
		for (const FGMCAttributeModifier& Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, true);
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
	if( ( EffectData.MustHaveTags.Num() > 0 && !DoesOwnerHaveTagFromContainer(EffectData.MustHaveTags) ) ||
		DoesOwnerHaveTagFromContainer(EffectData.MustNotHaveTags) )
	{
		EndEffect();
	}


	// If there's a period, check to see if it's time to tick
	if (!IsPeriodPaused() && EffectData.Period > 0 && CurrentState == EEffectState::Started)
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

void UGMCAbilityEffect::PeriodTick()
{
	for (const FGMCAttributeModifier& AttributeModifier : EffectData.Modifiers)
	{
		OwnerAbilityComponent->ApplyAbilityEffectModifier(AttributeModifier);
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
			if (EffectData.Duration != 0 && OwnerAbilityComponent->ActionTimer >= EffectData.EndTime)
			{
				EndEffect();
			}
			break;
		case EEffectState::Ended:
			break;
	default: break;
	}
}
