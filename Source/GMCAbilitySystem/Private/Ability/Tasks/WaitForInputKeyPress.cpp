// © Deep Worlds SA
#include "Ability/Tasks/WaitForInputKeyPress.h"

#include "EnhancedInputComponent.h"
#include "SNegativeActionButton.h"
#include "Components/GMCAbilityComponent.h"


UGMCAbilityTask_WaitForInputKeyPress* UGMCAbilityTask_WaitForInputKeyPress::WaitForKeyPress(UGMCAbility* OwningAbility)
{
	UGMCAbilityTask_WaitForInputKeyPress* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyPress>(OwningAbility);
	Task->Ability = OwningAbility;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyPress::Activate()
{
	Super::Activate();

	if (Ability->bAllowMultipleInstances) {
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability %s is set to allow multiple instances and this should not be used with WaitForInputKeyPress AbilityTask !"), *Ability->GetName());
		ClientProgressTask();
		return;
	}
	
	if (Ability->AbilityInputAction != nullptr)
	{
		InputActionBinding = &GetEnhancedInputComponent()->BindActionValue(Ability->AbilityInputAction);
	}
	else
	{
		ClientProgressTask();
	}
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyPress::GetEnhancedInputComponent() const
{
	UInputComponent* InputComponent = Ability->OwnerAbilityComponent->GetOwner()->GetComponentByClass<UInputComponent>();
	if (InputComponent)
	{
		if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent))
		{
			return EnhancedInputComponent;
		}
	}
	return nullptr;
}

void UGMCAbilityTask_WaitForInputKeyPress::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bTaskCompleted) {
		return;
	}
	
	if (AbilitySystemComponent->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	
	
	if(InputActionBinding == nullptr || (InputActionBinding->GetValue().Get<bool>() && bFirstReleaseTriggerPass))
	{
		ClientProgressTask();
	}
	else if (!bFirstReleaseTriggerPass && !InputActionBinding->GetValue().Get<bool>())
	{
		bFirstReleaseTriggerPass = true;
	}
}

void UGMCAbilityTask_WaitForInputKeyPress::OnTaskCompleted()
{
	EndTask();
	Completed.Broadcast();
	bTaskCompleted = true;
}

void UGMCAbilityTask_WaitForInputKeyPress::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForInputKeyPress::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	OnTaskCompleted();
}

void UGMCAbilityTask_WaitForInputKeyPress::ClientProgressTask()
{
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}

