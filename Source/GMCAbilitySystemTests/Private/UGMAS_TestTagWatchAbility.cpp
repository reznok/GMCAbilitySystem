#include "UGMAS_TestTagWatchAbility.h"

void UGMAS_TestTagWatchAbility::BeginAbility()
{
	Super::BeginAbility();
	if (AbilityState == EAbilityState::Ended) return;   // cancelled inside Super: no task on an ended ability

	// The instance's own WatchTag / WatchType: TryActivateAbility copies every CDO property.
	FGameplayTagContainer WatchContainer;
	WatchContainer.AddTag(WatchTag);

	UGMCAbilityTask_WaitForGameplayTagChange* Task =
		UGMCAbilityTask_WaitForGameplayTagChange::WaitForGameplayTagChange(
			this, WatchContainer, WatchType);

	Task->Completed.AddDynamic(this, &UGMAS_TestTagWatchAbility::OnTagChanged);
	Task->ReadyForActivation();
}

void UGMAS_TestTagWatchAbility::OnTagChanged(FGameplayTagContainer MatchedTags)
{
	EndAbility();
}
