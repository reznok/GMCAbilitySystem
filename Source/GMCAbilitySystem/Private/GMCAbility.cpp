#include "GMCAbility.h"

#include "GMCAbilitySystem.h"
#include "Components/GMCAbilityComponent.h"

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

void UGMCAbility::Tick(float DeltaTime)
{
	TickTasks(DeltaTime);
}

void UGMCAbility::TickTasks(float DeltaTime)
{
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) {continue;}
		Task.Value->Tick(DeltaTime);
	}
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

void UGMCAbility::ProgressTask(int Task)
{
	if (RunningTasks.Contains(Task))
	{
		RunningTasks[Task]->ProgressTask();
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
	if (!AbilityTask)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbility::OnGameplayTaskInitialized called with non-UGMCAbilityTaskBase task"));
		return;
	}
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

bool UGMCAbility::CheckActivationTags()
{
	// Required Tags
	for (const FGameplayTag Tag : ActivationRequiredTags)
	{
		if (!AbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}

	// Blocking Tags
	for (const FGameplayTag Tag : ActivationBlockedTags)
	{
		if (AbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}

	return true;
}

void UGMCAbility::BeginAbility(FGMCAbilityData AbilityData)
{
	// Check Activation Tags
	if (!CheckActivationTags()){
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability Activation Stopped By Tags"));
		return;
	}
	
	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Execute BP Event
	BeginAbilityEvent(AbilityData);
}

void UGMCAbility::EndAbility()
{
	// RunningTasks.Empty();
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		Task.Value->EndTask();
	}
	
	AbilityState = EAbilityState::Ended;
	EndAbilityEvent();
}
