#include "Ability/GMCAbility.h"
#include "GMCAbilitySystem.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "Components/GMCAbilityComponent.h"

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
	TickEvent(DeltaTime);
}

void UGMCAbility::TickTasks(float DeltaTime)
{
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) {continue;}
		Task.Value->Tick(DeltaTime);
	}
}

void UGMCAbility::Execute(UGMC_AbilitySystemComponent* InAbilityComponent, int InAbilityID, UInputAction* InputAction)
{
	this->AbilityKey = InputAction;
	this->AbilityID = InAbilityID;
	this->OwnerAbilityComponent = InAbilityComponent;
	BeginAbility();
}

bool UGMCAbility::CanAffordAbilityCost() const
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return true;
	
	UGMCAbilityEffect* AbilityEffect = AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	for (FGMCAttributeModifier AttributeModifier : AbilityEffect->EffectData.Modifiers)
	{
		for (const FAttribute* Attribute : OwnerAbilityComponent->GetAllAttributes())
		{
			if (Attribute->Tag.MatchesTagExact(AttributeModifier.AttributeTag))
			{
				if (Attribute->Value + AttributeModifier.Value < 0) return false;
			}
		}
	}

	return true;
}

void UGMCAbility::CommitAbilityCost()
{
	if (AbilityCost == nullptr || OwnerAbilityComponent == nullptr) return;
	
	const UGMCAbilityEffect* EffectCDO = DuplicateObject(AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this);
	FGMCAbilityEffectData EffectData = EffectCDO->EffectData;
	EffectData.OwnerAbilityComponent = OwnerAbilityComponent;
	OwnerAbilityComponent->ApplyAbilityEffect(DuplicateObject(EffectCDO, this), EffectData);
}

void UGMCAbility::ProgressTask(int Task, FInstancedStruct TaskData)
{
	if (RunningTasks.Contains(Task))
	{
		RunningTasks[Task]->ProgressTask(TaskData);
	}
}

UGameplayTasksComponent* UGMCAbility::GetGameplayTasksComponent(const UGameplayTask& Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent; }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskOwner(const UGameplayTask* Task) const
{
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
	return nullptr;
}

AActor* UGMCAbility::GetGameplayTaskAvatar(const UGameplayTask* Task) const
{
	// Wtf is avatar?
	if (OwnerAbilityComponent != nullptr) { return OwnerAbilityComponent->GetOwner(); }
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
	AbilityTask->SetAbilitySystemComponent(OwnerAbilityComponent);
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
		if (!OwnerAbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}

	// Blocking Tags
	for (const FGameplayTag Tag : ActivationBlockedTags)
	{
		if (OwnerAbilityComponent->HasActiveTag(Tag))
		{
			return false;
		}
	}

	return true;
}

void UGMCAbility::BeginAbility()
{
	// Check Activation Tags
	if (!CheckActivationTags()){
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Tags"), *AbilityTag.ToString());
		EndAbility();
		return;
	}
	
	// Initialize Ability
	AbilityState = EAbilityState::Initialized;

	// Execute BP Event
	BeginAbilityEvent();
}

void UGMCAbility::EndAbility()
{
	// RunningTasks.Empty();
	for (const TPair<int, UGMCAbilityTaskBase* >& Task : RunningTasks)
	{
		if (Task.Value == nullptr) continue;
		Task.Value->EndTask();
	}
	
	AbilityState = EAbilityState::Ended;
	EndAbilityEvent();
}

AActor* UGMCAbility::GetOwnerActor() const
{
	return OwnerAbilityComponent->GetOwner();
}

float UGMCAbility::GetOwnerAttributeValueByTag(FGameplayTag AttributeTag) const
{
	return GetAttributeValueByTag(OwnerAbilityComponent, AttributeTag);
}

float UGMCAbility::GetAttributeValueByTag(const UGMC_AbilitySystemComponent* AbilityComponent, const FGameplayTag AttributeTag)
{
	if (AbilityComponent == nullptr || AttributeTag == FGameplayTag::EmptyTag)
	{
		return 0;
	}
	
	if (const FAttribute* Att = AbilityComponent->GetAttributeByTag(AttributeTag))
	{
		return Att->Value;
	}

	return 0;
}

void UGMCAbility::SetOwnerJustTeleported(bool bValue)
{
	OwnerAbilityComponent->bJustTeleported = bValue;
}
