#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

bool FGMCAbilityData::operator==(FGMCAbilityData const& Other) const
{
	return AbilityActivationID == Other.AbilityActivationID && AbilityTag == Other.AbilityTag;
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

void UGMCAbility::Execute(UGMC_AbilityComponent* InAbilityComponent, FGMCAbilityData AbilityData)
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
		// RunningTasks[Task]->CompleteTask(false);
	}
}

bool UGMCAbility::HasAuthority()
{
	return AbilityComponent->GetOwner()->GetLocalRole() == ROLE_Authority;
}

UGameplayTasksComponent* UGMCAbility::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	if (AbilityComponent != nullptr) { return AbilityComponent; }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (AbilityComponent != nullptr) { return AbilityComponent->GetOwner(); }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	// Wtf is avatar?
	if (AbilityComponent != nullptr) { return AbilityComponent->GetOwner(); }
	return nullptr;
}

void UGMCAbility::OnGameplayTaskInitialized(UGameplayTask& Task)
{
	UGMCAbilityTaskBase* AbilityTask = Cast<UGMCAbilityTaskBase>(&Task);
	AbilityTask->SetAbilitySystemComponent(AbilityComponent);
	AbilityTask->Ability = this;
	
}

void UGMCAbility::OnGameplayTaskActivated(UGameplayTask& Task)
{
	ActiveTasks.Add(&Task);
}

void UGMCAbility::OnGameplayTaskDeactivated(UGameplayTask& Task)
{
	ActiveTasks.Remove(&Task);
}

void UGMCAbility::BeginAbility(FGMCAbilityData AbilityData)
{
	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Execute BP Event
	BeginAbilityEvent(AbilityData);
}

void UGMCAbility::EndAbility()
{
	TArray<int> OldTasks;
	// RunningTasks.Empty();
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		// Task.Value->CompleteTask(true);
	}
	for (int TaskId : OldTasks)
	{
		RunningTasks.Remove(TaskId);
	}
	
	AbilityState = EAbilityState::Ended;
	EndAbilityEvent();
}
