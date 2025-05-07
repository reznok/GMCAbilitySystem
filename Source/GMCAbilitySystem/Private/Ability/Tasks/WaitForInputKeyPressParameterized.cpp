#include "Ability/Tasks/WaitForInputKeyPressParameterized.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/GMCAbilityComponent.h"

UGMCAbilityTask_WaitForInputKeyPressParameterized* UGMCAbilityTask_WaitForInputKeyPressParameterized::WaitForKeyPress(UGMCAbility* OwningAbility, UInputAction* InputAction, bool bCheckForPressDuringActivation, float MaxDuration)
{
	UGMCAbilityTask_WaitForInputKeyPressParameterized* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyPressParameterized>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->InputActionToWaitFor = InputAction;
	Task->MaxDuration = MaxDuration;
	Task->bShouldCheckForPressDuringActivation = bCheckForPressDuringActivation;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::Activate()
{
	Super::Activate();

	StartTime = AbilitySystemComponent->ActionTimer;

	if (Ability->bAllowMultipleInstances) {
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability %s is set to allow multiple instances and this should not be used with WaitForInputKeyPress AbilityTask !"), *Ability->GetName());
		ClientProgressTask();
		return;
	}

	if (Ability->AbilityInputAction != nullptr)
	{
		UEnhancedInputComponent* const EnhInputComponent = GetEnhancedInputComponent();

		if (InputComponent)
		{
			const FEnhancedInputActionEventBinding& Binding = EnhInputComponent->BindAction(
				InputActionToWaitFor, ETriggerEvent::Started, this,
				&UGMCAbilityTask_WaitForInputKeyPressParameterized::OnKeyPressed);

			InputBindingHandle = Binding.GetHandle();

			// Check if button was held when entering the task
			if (bShouldCheckForPressDuringActivation)
			{
				FInputActionValue ActionValue = FInputActionValue();
				APlayerController* PC = AbilitySystemComponent->GetOwner()->GetInstigatorController<APlayerController>();
				if (UEnhancedInputLocalPlayerSubsystem* InputSubSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
					ActionValue = InputSubSystem->GetPlayerInput() ? InputSubSystem->GetPlayerInput()->GetActionValue(Ability->AbilityInputAction) : FInputActionValue();
				}
				if (ActionValue.GetMagnitude() == 1)
				{
					InputBindingHandle = -1;
					ClientProgressTask();
				}
			}
		}
	}
	else
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::AncillaryTick(float DeltaTime)
{
	Super::AncillaryTick(DeltaTime);

	if (bTaskCompleted) return;
	
	Duration = AbilitySystemComponent->ActionTimer - StartTime;
	OnTick.Broadcast(Duration);

	if (MaxDuration > 0 && Duration >= MaxDuration)
	{
		ClientProgressTask();
		bTimedOut = true;
	}
	
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::OnKeyPressed(const FInputActionValue& InputActionValue)
{
	// Unbind from the input component so we don't fire multiple times.
	if (UEnhancedInputComponent* EnhInputComponent = GetValid(GetEnhancedInputComponent()))
	{
		EnhInputComponent->RemoveActionBindingForHandle(InputBindingHandle);
		InputBindingHandle = -1;
	}

	ClientProgressTask();
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyPressParameterized::GetEnhancedInputComponent()
{
	InputComponent = Ability->OwnerAbilityComponent->GetOwner()->GetComponentByClass<UInputComponent>();
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
	Duration = AbilitySystemComponent->ActionTimer - StartTime;
	if (!bTimedOut)
	{
		Completed.Broadcast(Duration);
	}
	else
	{
		TimedOut.Broadcast(Duration);
	}
	bTaskCompleted = true;
}

void UGMCAbilityTask_WaitForInputKeyPressParameterized::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);

	// If we're still bound to the input component for some reason, we'll want to unbind.
	if (InputBindingHandle != -1)
	{
		if (InputComponent)
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
