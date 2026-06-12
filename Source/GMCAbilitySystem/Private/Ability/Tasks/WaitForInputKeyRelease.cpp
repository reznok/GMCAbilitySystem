#include "Ability/Tasks/WaitForInputKeyRelease.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Components/GMCAbilityComponent.h"
#include "Kismet/KismetSystemLibrary.h"

UGMCAbilityTask_WaitForInputKeyRelease* UGMCAbilityTask_WaitForInputKeyRelease::WaitForKeyRelease(UGMCAbility* OwningAbility, bool bCheckForReleaseDuringActivation, float MaxDuration)
{
	UGMCAbilityTask_WaitForInputKeyRelease* Task = NewAbilityTask<UGMCAbilityTask_WaitForInputKeyRelease>(OwningAbility);
	Task->Ability = OwningAbility;
	Task->bShouldCheckForReleaseDuringActivation = bCheckForReleaseDuringActivation;
	Task->MaxDuration = MaxDuration;
	return Task;
}

void UGMCAbilityTask_WaitForInputKeyRelease::Activate()
{
	Super::Activate();
	
	StartTime = AbilitySystemComponent->ActionTimer;

	UEnhancedInputComponent* const InputComponent = GetEnhancedInputComponent();
	
	if (Ability->AbilityInputAction != nullptr && InputComponent != nullptr)
	{
		FEnhancedInputActionEventBinding& Binding = InputComponent->BindAction(
			Ability->AbilityInputAction, ETriggerEvent::Completed, this,
			&UGMCAbilityTask_WaitForInputKeyRelease::OnKeyReleased);

		InputBindingHandle = Binding.GetHandle();
		
		// Check that the value isn't currently false.
		// Only the locally-controlled client (or listen server host) can read the real key state.
		// On a dedicated server / remote pawn, PC->GetLocalPlayer() is null, the magnitude check
		// silently sees 0, and we'd queue a Progress payload server-side that ends the task before
		// the client's release ever arrives — the task would never be "confirmed" by the server.
		//
		// Replay-safety: during a GMC replay the ability re-activates from move history, but the
		// EnhancedInput read below reflects the LIVE key state (now released), not the state at the
		// replayed timestamp. Running the EndOnStart path here would end the task — and drop any
		// persistent effect the ability holds (e.g. a Set on the Speed attribute) — on the client
		// only, while the server keeps it, yielding a bound-attribute/location divergence and a
		// replay cascade. The legitimate end is already captured: the original activation queued a
		// Progress via ClientProgressTask (QueueTaskData), which replays deterministically. So skip
		// the live poll while replaying.
		if (bShouldCheckForReleaseDuringActivation && IsClientOrRemoteListenServerPawn()
			&& !AbilitySystemComponent->IsReplayingForGMASLogic())
		{
			FInputActionValue ActionValue = FInputActionValue();
			// PC can be null during possession transitions (and GetLocalPlayer on a remote PC) —
			// guard the chain instead of dereferencing blindly.
			APlayerController* PC = AbilitySystemComponent->GetOwner()->GetInstigatorController<APlayerController>();
			if (UEnhancedInputLocalPlayerSubsystem* InputSubSystem = PC ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()) : nullptr) {
				ActionValue = InputSubSystem->GetPlayerInput() ? InputSubSystem->GetPlayerInput()->GetActionValue(Ability->AbilityInputAction) : FInputActionValue();
			}

			if (ActionValue.GetMagnitude() == 0)
			{
				UE_LOG(LogGMCAbilitySystem, Error, TEXT("UGMCAbilityTask_WaitForInputKeyRelease::Activate: EndOnStart!"));
				// We'll want to immediately unbind the binding.
				InputComponent->RemoveActionBindingForHandle(Binding.GetHandle());
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

void UGMCAbilityTask_WaitForInputKeyRelease::AncillaryTick(float DeltaTime)
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

void UGMCAbilityTask_WaitForInputKeyRelease::OnKeyReleased(const FInputActionValue& InputActionValue)
{
	// Unbind since we're done now.
	ClientProgressTask();
	InputBindingHandle = -1;
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

void UGMCAbilityTask_WaitForInputKeyRelease::OnTaskCompleted()
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

void UGMCAbilityTask_WaitForInputKeyRelease::OnDestroy(bool bInOwnerFinished)
{
	// If the handle is still valid somehow, unbind it. Done BEFORE Super per the engine
	// contract ("call Super::OnDestroy(bOwnerFinished) as the last thing").
	if (InputBindingHandle != -1)
	{
		if (UInputComponent* const InputComponent = GetValid(GetEnhancedInputComponent()))
		{
			InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
		}

		InputBindingHandle = -1;
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForInputKeyRelease::ProgressTask(FInstancedStruct& TaskData)
{
	Super::ProgressTask(TaskData);
	OnTaskCompleted();
}

void UGMCAbilityTask_WaitForInputKeyRelease::ClientProgressTask()
{
	if (UInputComponent* const InputComponent = GetValid(GetEnhancedInputComponent()))
	{
		InputComponent->RemoveActionBindingForHandle(InputBindingHandle);
	}
	
	FGMCAbilityTaskData TaskData;
	TaskData.TaskType = EGMCAbilityTaskDataType::Progress;
	TaskData.AbilityID = Ability->GetAbilityID();
	TaskData.TaskID = TaskID;
	const FInstancedStruct TaskDataInstance = FInstancedStruct::Make(TaskData);
	
	Ability->OwnerAbilityComponent->QueueTaskData(TaskDataInstance);
}

