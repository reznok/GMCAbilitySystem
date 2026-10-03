#include "Tests/UGMAS_TestTagWatchAbility.h"

void UGMAS_TestTagWatchAbility::BeginAbility()
{
	Super::BeginAbility();

	const UGMAS_TestTagWatchAbility* CDO = GetDefault<UGMAS_TestTagWatchAbility>();

	FGameplayTagContainer WatchContainer;
	WatchContainer.AddTag(CDO->WatchTag);

	UGMCAbilityTask_WaitForGameplayTagChange* Task =
		UGMCAbilityTask_WaitForGameplayTagChange::WaitForGameplayTagChange(
			this, WatchContainer, CDO->WatchType);

	Task->Completed.AddDynamic(this, &UGMAS_TestTagWatchAbility::OnTagChanged);
	Task->ReadyForActivation();
}

void UGMAS_TestTagWatchAbility::OnTagChanged(FGameplayTagContainer MatchedTags)
{
	EndAbility();
}
