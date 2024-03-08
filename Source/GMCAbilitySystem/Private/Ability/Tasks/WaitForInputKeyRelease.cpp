#include "Ability/Tasks/WaitForInputKeyRelease.h"

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
	
	if (Ability->AbilityInputAction != nullptr)
	{
		InputActionBinding = &GetEnhancedInputComponent()->BindActionValue(Ability->AbilityInputAction);
	}
	else
	{
		ClientProgressTask();
	}
}

UEnhancedInputComponent* UGMCAbilityTask_WaitForInputKeyRelease::GetEnhancedInputComponent() const
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

void UGMCAbilityTask_WaitForInputKeyRelease::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	if (bTaskCompleted) {return;}
	
	if (AbilitySystemComponent->GetNetMode() == NM_DedicatedServer) return;
	
	if(InputActionBinding == nullptr || !InputActionBinding->GetValue().Get<bool>())
	{
		ClientProgressTask();
	}
}

void UGMCAbilityTask_WaitForInputKeyRelease::OnTaskCompleted()
{
	EndTask();
	Completed.Broadcast();
	bTaskCompleted = true;
}

void UGMCAbilityTask_WaitForInputKeyRelease::OnDestroy(bool bInOwnerFinished)
{
	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForInputKeyRelease::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	OnTaskCompleted();
}

void UGMCAbilityTask_WaitForInputKeyRelease::ClientProgressTask()
{
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}

