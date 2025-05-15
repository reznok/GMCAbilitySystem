#include "Ability/Tasks/WaitForInputKeyPress.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/GMCAbilityComponent.h"
#include "Kismet/KismetSystemLibrary.h"

UGMCAbilityTask_WaitForInputKeyPress* UGMCAbilityTask_WaitForInputKeyPress::WaitForKeyPress(UGMCAbility* OwningAbility, bool bCheckForPressDuringActivation, float MaxDuration)
{
	UGMCAbilityTask_WaitForInputKeyPress* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyPress>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->bShouldCheckForPressDuringActivation = bCheckForPressDuringActivation;
	Task->MaxDuration = MaxDuration;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyPress::Activate()
{
	Super::Activate();
	
	StartTime = AbilitySystemComponent->ActionTimer;
	
	if (Ability->bAllowMultipleInstances) {
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("Ability %s is set to allow multiple instances and this should not be used with WaitForInputKeyPress AbilityTask !"), *Ability->GetName());
		ClientProgressTask();
		return;
	}
	
	UEnhancedInputComponent* EnhancedInputComponent = GetEnhancedInputComponent();
	
	if (Ability->AbilityInputAction != nullptr && InputComponent != nullptr)
	{
		const FEnhancedInputActionEventBinding& Binding = EnhancedInputComponent->BindAction(
			Ability->AbilityInputAction, ETriggerEvent::Started, this,
			&UGMCAbilityTask_WaitForInputKeyPress::OnKeyPressed);

		
	
		InputBindingHandle = Binding.GetHandle();

		// Check if button was held when entering the task
		if (bShouldCheckForPressDuringActivation)
		{
			FInputActionValue ActionValue = FInputActionValue();
			APlayerController* PC = AbilitySystemComponent->GetOwner()->GetInstigatorController<APlayerController>();
			if (UEnhancedInputLocalPlayerSubsystem* InputSubSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer())) {
				ActionValue = InputSubSystem->GetPlayerInput() ? InputSubSystem->GetPlayerInput()->GetActionValue(Ability->AbilityInputAction) : FInputActionValue();
			}
			
			if (!ActionValue.GetMagnitude())
			{
				InputBindingHandle = -1;
				ClientProgressTask();
			}
		}
	}
	else
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_WaitForInputKeyPress::AncillaryTick(float DeltaTime)
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

void UGMCAbilityTask_WaitForInputKeyPress::OnKeyPressed(const FInputActionValue& InputActionValue)
{
	
	// Unbind from the input component so we don't fire multiple times.
	if (InputComponent)
	{
		InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
		InputBindingHandle = -1;
	}
	
	ClientProgressTask();
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyPress::GetEnhancedInputComponent()
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

void UGMCAbilityTask_WaitForInputKeyPress::OnTaskCompleted()
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

void UGMCAbilityTask_WaitForInputKeyPress::OnDestroy(bool bInOwnerFinished)
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

