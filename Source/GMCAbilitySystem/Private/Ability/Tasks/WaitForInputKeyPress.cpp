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

		// Check if button was held when entering the task.
		// Only the locally-controlled client (or listen server host) can read the real key state.
		// On a dedicated server / remote pawn, PC->GetLocalPlayer() is null, the magnitude check
		// silently sees 0, and we'd queue a Progress payload server-side that ends the task before
		// the client's press ever arrives — the task would never be "confirmed" by the server.
		if (bShouldCheckForPressDuringActivation && IsClientOrRemoteListenServerPawn())
		{
			FInputActionValue ActionValue = FInputActionValue();
			// PC can be null during possession transitions (and GetLocalPlayer on a remote PC) —
			// guard the chain instead of dereferencing blindly.
			APlayerController* PC = AbilitySystemComponent->GetOwner()->GetInstigatorController<APlayerController>();
			if (UEnhancedInputLocalPlayerSubsystem* InputSubSystem = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr) {
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
		// Progress push only where a queue drain exists (owning client / listen host): on a
		// dedicated server this re-queued every ancillary tick into QueuedTaskData, which is
		// never drained for remote pawns — unbounded growth for the remaining ability life.
		// bTimedOut must still latch server-side so the payload-driven OnTaskCompleted picks
		// the TimedOut broadcast on both machines.
		if (IsClientOrRemoteListenServerPawn())
		{
			ClientProgressTask();
		}
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
	// Completion latch BEFORE the broadcast: a re-entrant Completed handler (or a replayed
	// Progress payload) must see the task as already done, not run the completion twice.
	if (bTaskCompleted) return;
	bTaskCompleted = true;

	EndTask();
	Duration = AbilitySystemComponent->ActionTimer - StartTime;
	// Deterministic timeout decision: the ancillary-tick latch races the payload dispatch
	// across machines (the server's latch runs AFTER the payload dispatch point, so when the
	// threshold-crossing move and the payload-carrying move batch into the same server frame,
	// the server would read a stale bTimedOut=false while the client latched true). Re-derive
	// from the payload-move ActionTimer, which both machines share, so Completed vs TimedOut
	// never diverges.
	if (!bTimedOut && MaxDuration > 0 && Duration >= MaxDuration)
	{
		bTimedOut = true;
	}
	if (!bTimedOut)
	{
		Completed.Broadcast(Duration);
	}
	else
	{
		TimedOut.Broadcast(Duration);
	}
}

void UGMCAbilityTask_WaitForInputKeyPress::OnDestroy(bool bInOwnerFinished)
{
	// If we're still bound to the input component for some reason, we'll want to unbind.
	// Done BEFORE Super per the engine contract ("call Super::OnDestroy as the last thing").
	if (InputBindingHandle != -1)
	{
		if (InputComponent)
		{
			InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
			InputBindingHandle = -1;
		}
	}

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

