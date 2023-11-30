#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

bool FGMCAbilityData::operator==(FGMCAbilityData const& Other) const
{
	return AbilityActivationID == Other.AbilityActivationID && GrantedAbilityIndex == Other.GrantedAbilityIndex;
}

FString FGMCAbilityData::ToStringSimple() const
{
	FString RetString = FString::Printf(TEXT("ID: %d"), AbilityActivationID);
	return RetString;
}

UWorld* UGMCAbility::GetWorld() const
{
#if WITH_EDITOR
	if (GIsEditor)
	{
		return GWorld;
	}
#endif // WITH_EDITOR
	return GEngine->GetWorldContexts()[0].World();
}

void UGMCAbility::Execute(FGMCAbilityData AbilityData, UGMC_AbilityComponent* InAbilityComponent)
{
	this->InitialAbilityData = AbilityData;
	this->AbilityComponent = InAbilityComponent;
	BeginAbility(AbilityData);
}

void UGMCAbility::CommitAbilityCost()
{
	if (AbilityComponent)
	{
		AbilityComponent->ApplyAbilityCost(this);
	}
}

void UGMCAbility::CompleteLatentTask(int Task)
{
	if (RunningTasks.Contains(Task))
	{
		RunningTasks[Task]->CompleteTask(false);
	}
}

bool UGMCAbility::HasAuthority()
{
	return AbilityComponent->GetOwner()->GetLocalRole() == ROLE_Authority;
}

void UGMCAbility::BeginAbility(FGMCAbilityData AbilityData)
{
	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Execute BP Event
	BeginAbilityBP(AbilityData);
}

void UGMCAbility::EndAbility()
{
	TArray<int> OldTasks;
	// RunningTasks.Empty();
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		Task.Value->CompleteTask(true);
	}
	for (int TaskId : OldTasks)
	{
		RunningTasks.Remove(TaskId);
	}
	
	AbilityState = EAbilityState::Ended;
}
