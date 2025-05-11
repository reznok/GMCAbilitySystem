// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StateTreeGMASComponent.h"
#include "StateTreeExecutionContext.h"
#include "Components/StateTreeGMASComponentSchema.h"
#include "Util/GMASAIEvents.h"

TSubclassOf<UStateTreeSchema> UStateTreeGMASComponent::GetSchema() const
{
	return UStateTreeGMASComponentSchema::StaticClass();
}

bool UStateTreeGMASComponent::SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors)
{
	Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &UStateTreeGMASComponent::CollectExternalData));
	return UStateTreeGMASComponentSchema::SetContextRequirements(*this, Context, bLogErrors);
}

void UStateTreeGMASComponent::OnActiveTagsChanged(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags)
{
	FGMAS_AIEventActiveTagsChanged TagsChanged;
	TagsChanged.AddedTags = AddedTags;
	TagsChanged.RemovedTags = RemovedTags;
	
	const FConstStructView Payload = FConstStructView::Make(TagsChanged);
	SendStateTreeEvent(TAG_GMAS_AI_Event_ActiveTagsChanged, Payload, GetOwner()->GetFName());
}

void UStateTreeGMASComponent::BeginPlay()
{
	Super::BeginPlay();
	AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(GetOwner()->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
	if (!AbilitySystemComponent)
	{
		UE_LOG(LogTemp, Error, TEXT("StateTreeGMASComponent: No AbilitySystemComponent found on owner %s"), *GetOwner()->GetName());
	}

	AbilitySystemComponent->OnActiveTagsChanged.AddUniqueDynamic(this, &UStateTreeGMASComponent::OnActiveTagsChanged);
}
