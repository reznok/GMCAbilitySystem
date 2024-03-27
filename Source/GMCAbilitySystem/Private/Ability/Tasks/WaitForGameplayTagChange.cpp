// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Tasks/WaitForGameplayTagChange.h"

UGMCAbilityTask_WaitForGameplayTagChange* UGMCAbilityTask_WaitForGameplayTagChange::WaitForGameplayTagChange(
	UGMCAbility* OwningAbility, const FGameplayTagContainer& WatchedTags, EGMCWaitForGameplayTagChangeType ChangeType)
{
	UGMCAbilityTask_WaitForGameplayTagChange* Task = NewAbilityTask<UGMCAbilityTask_WaitForGameplayTagChange>(OwningAbility);
	Task->Tags = WatchedTags;
	Task->ChangeType = ChangeType;
	return Task;
}

void UGMCAbilityTask_WaitForGameplayTagChange::Activate()
{
	Super::Activate();

	Ability->OwnerAbilityComponent->AddFilteredTagChangeDelegate(Tags, FGameplayTagFilteredMulticastDelegate::FDelegate::CreateUObject(this, &UGMCAbilityTask_WaitForGameplayTagChange::OnGameplayTagChanged));
}

void UGMCAbilityTask_WaitForGameplayTagChange::OnGameplayTagChanged(const FGameplayTagContainer& AddedTags,
	const FGameplayTagContainer& RemovedTags)
{
	FGameplayTagContainer MatchedTags;
	
	switch (ChangeType)
	{
		case Set:
			MatchedTags = AddedTags.Filter(Tags);
			break;

		case Unset:
			MatchedTags = AddedTags.Filter(Tags);
			break;

		case Changed:
			MatchedTags = AddedTags.Filter(Tags);
			MatchedTags.AppendTags(RemovedTags.Filter(Tags));
			break;
	}

	if (!MatchedTags.IsEmpty())
	{
		Completed.Broadcast(MatchedTags);
		EndTask();
	}
}
