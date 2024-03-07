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
	if (!OwnerAbilityComponent->HasAuthority())
	{
		if (!bServerConfirmed && ClientStartTime + ServerConfirmTimeout < OwnerAbilityComponent->ActionTimer)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Ability Not Confirmed By Server: %d, Removing..."), AbilityID);
			EndAbility();
			return;
		}
	}
	
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
<<<<<<< HEAD
	this->AbilityInputAction = InputAction;
=======
	this->AbilityInputAction  = InputAction;
>>>>>>> 278dcad... tagtargetdata task, parent tag helper function, update input action var on ability
	this->AbilityID = InAbilityID;
	this->OwnerAbilityComponent = InAbilityComponent;
	this->ClientStartTime = InAbilityComponent->ActionTimer;
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
	AbilityCostInstance = OwnerAbilityComponent->ApplyAbilityEffect(DuplicateObject(EffectCDO, this), EffectData);
}

void UGMCAbility::RemoveAbilityCost(){
	if(AbilityCostInstance){
		OwnerAbilityComponent->RemoveActiveAbilityEffect(AbilityCostInstance);
	}
}

void UGMCAbility::HandleTaskData(int TaskID, FInstancedStruct TaskData)
{
	const FGMCAbilityTaskData TaskDataFromInstance = TaskData.Get<FGMCAbilityTaskData>();
	if (RunningTasks.Contains(TaskID) && RunningTasks[TaskID] != nullptr)
	{
		if (TaskDataFromInstance.TaskType == EGMCAbilityTaskDataType::Progress)
		{
			RunningTasks[TaskID]->ProgressTask(TaskData);
		}
		if (TaskDataFromInstance.TaskType == EGMCAbilityTaskDataType::Heartbeat)
		{
			RunningTasks[TaskID]->Heartbeat();
		}
	}
}

void UGMCAbility::ServerConfirm()
{
	bServerConfirmed = true;
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

bool UGMCAbility::IsOnCooldown() const
{
	return OwnerAbilityComponent->GetCooldownForAbility(AbilityTag) > 0;
}

void UGMCAbility::BeginAbility()
{
	// Check Activation Tags
	if (!CheckActivationTags()){
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Tags"), *AbilityTag.ToString());
		EndAbility();
		return;
	}

	if (IsOnCooldown())
	{
		UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("Ability Activation for %s Stopped By Cooldown"), *AbilityTag.ToString());
		EndAbility();
		return;
	}

	OwnerAbilityComponent->SetCooldownForAbility(AbilityTag, CooldownTime);
	
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
	return OwnerAbilityComponent->GetAttributeValueByTag(AttributeTag);
}


void UGMCAbility::SetOwnerJustTeleported(bool bValue)
{
	OwnerAbilityComponent->bJustTeleported = bValue;
}
