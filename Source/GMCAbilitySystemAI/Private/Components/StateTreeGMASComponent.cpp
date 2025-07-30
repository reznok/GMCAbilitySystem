// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StateTreeGMASComponent.h"

#include "AIController.h"
#include "GMCAbilitySystemAI.h"
#include "GMCPawn.h"
#include "StateTreeExecutionContext.h"
#include "Components/StateTreeGMASComponentSchema.h"
#include "Util/GMASAIEvents.h"

TSubclassOf<UStateTreeSchema> UStateTreeGMASComponent::GetSchema() const
{
	return UStateTreeGMASComponentSchema::StaticClass();
}

void UStateTreeGMASComponent::OnActiveTagsChanged(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags)
{
	FGMAS_AIEventActiveTagsChanged TagsChanged;
	TagsChanged.AddedTags = AddedTags;
	TagsChanged.RemovedTags = RemovedTags;
	
	const FConstStructView Payload = FConstStructView::Make(TagsChanged);
	SendStateTreeEvent(TAG_GMAS_AI_Event_ActiveTagsChanged, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::OnEffectApplied(UGMCAbilityEffect* AppliedEffect)
{
	FGMAS_AIEventEffectAdded EffectAdded;
	EffectAdded.Effect = AppliedEffect;

	const FConstStructView Payload = FConstStructView::Make(EffectAdded);
	SendStateTreeEvent(TAG_GMAS_AI_Event_EffectAdded, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::OnEffectRemoved(UGMCAbilityEffect* RemovedEffect)
{
	FGMAS_AIEventEffectRemoved EffectRemoved;
	EffectRemoved.Effect = RemovedEffect;
	const FConstStructView Payload = FConstStructView::Make(EffectRemoved);
	SendStateTreeEvent(TAG_GMAS_AI_Event_EffectRemoved, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::OnAbilityActivated(UGMCAbility* Ability, FGameplayTag AbilityTag)
{
	FGMAS_AIEventAbilityStarted AbilityStarted;
	AbilityStarted.Ability = Ability;
	const FConstStructView Payload = FConstStructView::Make(AbilityStarted);
	SendStateTreeEvent(TAG_GMAS_AI_Event_AbilityStarted, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::OnAbilityEnded(UGMCAbility* Ability)
{
	FGMAS_AIEventAbilityEnded AbilityEnded;
	AbilityEnded.Ability = Ability;

	const FConstStructView Payload = FConstStructView::Make(AbilityEnded);
	SendStateTreeEvent(TAG_GMAS_AI_Event_AbilityEnded, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::OnAttributeChanged(FGameplayTag AttributeTag, float OldValue, float NewValue)
{
	FGMAS_AIEventAttributeChanged AttributeChanged;
	AttributeChanged.AttributeTag = AttributeTag;
	AttributeChanged.OldValue = OldValue;
	AttributeChanged.NewValue = NewValue;
	const FConstStructView Payload = FConstStructView::Make(AttributeChanged);
	SendStateTreeEvent(TAG_GMAS_AI_Event_AttributeChanged, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::BeginPlay()
{
	Super::BeginPlay();
	if (!GetOwner()->HasAuthority()) return;
	
	if (GetOwner()->IsA(AGMC_Pawn::StaticClass()))
	{
		AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(GetOwner()->GetComponentByClass<UGMC_AbilitySystemComponent>());
	}
	else if (GetOwner()->IsA(AAIController::StaticClass()))
	{
		AAIController* AIController = Cast<AAIController>(GetOwner());
		AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(AIController->GetPawn()->GetComponentByClass<UGMC_AbilitySystemComponent>());
	}

	if (!AbilitySystemComponent)
	{
		UE_LOG(LogGMCAbilitySystemAI, Error, TEXT("StateTreeGMASComponent: Failed to find AbilitySystemComponent on %s"), *GetOwner()->GetName());
		return;
	}

	// Active Tags Changed
	AbilitySystemComponent->OnActiveTagsChanged.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnActiveTagsChanged);
	
	// Effect Applied
	AbilitySystemComponent->OnEffectApplied.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnEffectApplied);
	
	// Effect Removed
	AbilitySystemComponent->OnEffectRemoved.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnEffectRemoved);
	
	// Ability Activated
	AbilitySystemComponent->OnAbilityActivated.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnAbilityActivated);
	
	// Ability Ended
	AbilitySystemComponent->OnAbilityEnded.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnAbilityEnded);
	
	// Attribute Changed
	AbilitySystemComponent->OnAttributeChanged.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnAttributeChanged);
	
}
