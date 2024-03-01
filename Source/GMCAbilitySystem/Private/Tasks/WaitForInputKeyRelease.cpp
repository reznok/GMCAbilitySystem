﻿#include "Tasks/WaitForInputKeyRelease.h"

#include "EnhancedInputComponent.h"
#include "Components/GMCAbilityComponent.h"

UGMCAbilityTask_WaitForInputKeyRelease* UGMCAbilityTask_WaitForInputKeyRelease::WaitForKeyRelease(UGMCAbility* OwningAbility)
{
	UGMCAbilityTask_WaitForInputKeyRelease* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyRelease>(OwningAbility);
	Task->Ability = OwningAbility;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyRelease::Activate()
{
	Super::Activate();
	
	if (Ability->InitialAbilityData.ActionInput != nullptr)
	{
		InputReleaseBindHandle = GetEnhancedInputComponent()->BindAction(Ability->InitialAbilityData.ActionInput, ETriggerEvent::Completed, this, &UGMCAbilityTask_WaitForInputKeyRelease::ClientProgressTask).GetHandle();
	}
	
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyRelease::GetEnhancedInputComponent() const
{
	UInputComponent* InputComponent = Ability->AbilityComponent->GetOwner()->GetComponentByClass<UInputComponent>();
	if (InputComponent)
	{
		if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent))
		{
			return EnhancedInputComponent;
		}
	}
	return nullptr;
}

void UGMCAbilityTask_WaitForInputKeyRelease::TickTask(float DeltaTime)
{
	Super::TickTask(DeltaTime);
	
	if (bTaskCompleted)
	{		
		// InternalCompleted(false);
	}
}

void UGMCAbilityTask_WaitForInputKeyRelease::OnTaskCompleted()
{
	Completed.Broadcast();

	if (!AbilitySystemComponent->HasAuthority())
	{
		ClientProgressTask();
	}
	// Clean up. Calls OnDestroy.
	
	EndTask();
}

void UGMCAbilityTask_WaitForInputKeyRelease::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);
	
	if (InputReleaseBindHandle != -1)
	{
		GetEnhancedInputComponent()->RemoveBindingByHandle(InputReleaseBindHandle);
	}
}

void UGMCAbilityTask_WaitForInputKeyRelease::ProgressTask()
{
	Super::ProgressTask();
	OnTaskCompleted();
}

