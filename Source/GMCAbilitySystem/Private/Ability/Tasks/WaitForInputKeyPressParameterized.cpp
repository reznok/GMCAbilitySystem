#include "Ability/Tasks/WaitForInputKeyPressParameterized.h"

#include "EnhancedInputComponent.h"
#include "Components/GMCAbilityComponent.h"

UGMCAbilityTask_WaitForInputKeyPressParameterized* UGMCAbilityTask_WaitForInputKeyPressParameterized::WaitForKeyPress(UGMCAbility* OwningAbility, UInputAction* InputAction)
{
	UGMCAbilityTask_WaitForInputKeyPressParameterized* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyPressParameterized>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->InputActionToWaitFor = InputAction;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::Activate()
{
	Super::Activate();

	if (Ability->bAllowMultipleInstances) {
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability %s is set to allow multiple instances and this should not be used with WaitForInputKeyPress AbilityTask !"), *Ability->GetName());
		ClientProgressTask();
		return;
	}

	if (Ability->AbilityInputAction != nullptr)
	{
		UEnhancedInputComponent* const InputComponent = GetEnhancedInputComponent();

		if (InputComponent)
		{
			const FEnhancedInputActionEventBinding& Binding = InputComponent->BindAction(
				InputActionToWaitFor, ETriggerEvent::Started, this,
				&UGMCAbilityTask_WaitForInputKeyPressParameterized::OnKeyPressed);

			InputBindingHandle = Binding.GetHandle();
		}
	}
	else
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::OnKeyPressed(const FInputActionValue& InputActionValue)
{
	// Unbind from the input component so we don't fire multiple times.
	if (UEnhancedInputComponent* InputComponent = GetValid(GetEnhancedInputComponent()))
	{
		InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
		InputBindingHandle = -1;
	}

	ClientProgressTask();
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyPressParameterized::GetEnhancedInputComponent() const
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

void UGMCAbilityTask_WaitForInputKeyPressParameterized::OnTaskCompleted()
{
	EndTask();
	Completed.Broadcast();
	bTaskCompleted = true;
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);

	// If we're still bound to the input component for some reason, we'll want to unbind.
	if (InputBindingHandle != -1)
	{
		if (UEnhancedInputComponent* InputComponent = GetValid(GetEnhancedInputComponent()))
		{
			InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
			InputBindingHandle = -1;
		}
	}
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	OnTaskCompleted();
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::ClientProgressTask()
{
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);

	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}
