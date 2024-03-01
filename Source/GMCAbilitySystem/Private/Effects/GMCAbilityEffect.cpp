// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffect.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"


void UGMCAbilityEffect::InitializeEffect(FGMCAbilityEffectData InitializationData)
{
	EffectData = InitializationData;
	
	OwnerAbilityComponent = EffectData.OwnerAbilityComponent;
	SourceAbilityComponent = EffectData.SourceAbilityComponent;

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

void UGMCAbilityEffect::EndEffect()
{
	float time = OwnerAbilityComponent->ActionTimer;
	bCompleted = true;

	if (CurrentState != EEffectState::Ended)
	{
		UpdateState(EEffectState::Ended, true);
	}

	// Only remove tags and abilities if the effect has started
	if (!bHasStarted) return;

	if (EffectData.bNegateEffectAtEnd && !EffectData.bIsInstant)
	{
		for (const FGMCAttributeModifier Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier, true);
		}
	}
	
	RemoveTagsFromOwner();
	RemoveAbilitiesFromOwner();
}

void UGMCAbilityEffect::Tick(float DeltaTime, bool bIsReplayingPrediction)
{
	if (bCompleted) return;
	TickEvent(DeltaTime);
	
	// Ensure tag requirements are met before applying the effect
	if( !CheckMustHaveTags() || !CheckMustNotHaveTags() )
	{
		EndEffect();
	}

	// If there's a period, check to see if it's time to tick
	// This mess is to ensure that if the server sends a non predicted Period effect to the client
	// that the client will tick the effect at the correct times
	if (EffectData.Period > 0 && CurrentState == EEffectState::Started)
	{
		constexpr int Period_Precision = 10e7;
		
		// This precision may need to be altered for very low periods
		// BaseTime cannot be lower than the Period or it will not work
		const int BaseTime = FMath::RoundToInt((OwnerAbilityComponent->ActionTimer - EffectData.StartTime) * Period_Precision) + (EffectData.Period * Period_Precision);
		const int Mod = (BaseTime) % static_cast<int>(EffectData.Period * Period_Precision);
		
		// ActionTimer goes all over the place during replays, so we need to check if we're replaying a prediction
		if (Mod < PrevPeriodMod && !bIsReplayingPrediction)
		{
			PeriodTickEvent();
		}
		PrevPeriodMod = Mod;
	}
	
	CheckState();
}

void UGMCAbilityEffect::TickEvent_Implementation(float DeltaTime)
{
}

void UGMCAbilityEffect::PeriodTickEvent_Implementation()
{
	for (const FGMCAttributeModifier AttributeModifier : EffectData.Modifiers)
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

bool UGMCAbilityEffect::CheckMustHaveTags()
{
	for (const FGameplayTag Tag : EffectData.MustHaveTags)
	{
		if (!OwnerAbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}
	return true;
}

bool UGMCAbilityEffect::CheckMustNotHaveTags()
{
	for (const FGameplayTag Tag : EffectData.MustNotHaveTags)
	{
		if (OwnerAbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}
	return true;
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

void UGMCAbilityEffect::StartEffect()
{
	bHasStarted = true;

	// Ensure tag requirements are met before applying the effect
	if( !CheckMustHaveTags() || !CheckMustNotHaveTags() || DuplicateEffectAlreadyApplied() )
	{
		EndEffect();
		return;
	}
	
	AddTagsToOwner();
	AddAbilitiesToOwner();

	// Duration and Instant applies immediately.
	if (EffectData.Period == 0)
	{
		for (const FGMCAttributeModifier Modifier : EffectData.Modifiers)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifier(Modifier);
		}
	}

	// Tick period at start
	if (EffectData.bPeriodTickAtStart)
	{
		PeriodTickEvent();
	}
				
	// Instant effects instantly end
	if (EffectData.bIsInstant)
	{
		EndEffect();
	}

	UpdateState(EEffectState::Started, true);
}

bool UGMCAbilityEffect::CompletedAndServerConfirmed()
{
	return bCompleted && bServerConfirmed;
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

	int foo = KINDA_SMALL_NUMBER;
}
