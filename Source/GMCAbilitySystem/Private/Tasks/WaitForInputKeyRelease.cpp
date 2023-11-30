#include "Tasks/WaitForInputKeyRelease.h"

#include "EnhancedInputComponent.h"
#include "Components/GMCAbilityComponent.h"

UWaitForInputKeyReleaseAsyncAction* UWaitForInputKeyReleaseAsyncAction::WaitForKeyRelease(const UObject* WorldContext, UGMCAbility* Ability)
{
	UWorld* ContextWorld = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if(!ensureAlwaysMsgf(IsValid(WorldContext), TEXT("World Context was not valid.")))
	{
		return nullptr;
	}

	// Create a new UMyDelayAsyncAction, and store function arguments in it.
	UWaitForInputKeyReleaseAsyncAction* NewAction = NewObject<UWaitForInputKeyReleaseAsyncAction>();
	NewAction->OwningAbility = Ability;
	NewAction->ContextWorld = ContextWorld;
	NewAction->RegisterWithGameInstance(ContextWorld->GetGameInstance());
	return NewAction;
}

void UWaitForInputKeyReleaseAsyncAction::Activate()
{
	Super::Activate();
	
	StartTime = GetWorld()->GetRealTimeSeconds();

	if (OwningAbility->InitialAbilityData.ActionInput != nullptr)
	{
		InputReleaseBindHandle = GetEnhancedInputComponent()->BindAction(OwningAbility->InitialAbilityData.ActionInput, ETriggerEvent::Completed, this, &UWaitForInputKeyReleaseAsyncAction::InternalClientCompleted).GetHandle();
	}
	
	TickHandle = OwningAbility->AbilityComponent->OnFGMCAbilitySystemComponentTickDelegate.AddUObject(this, &UWaitForInputKeyReleaseAsyncAction::InternalTick);
}

UEnhancedInputComponent* UWaitForInputKeyReleaseAsyncAction::GetEnhancedInputComponent() const
{
	UInputComponent* InputComponent = OwningAbility->AbilityComponent->GetOwner()->GetComponentByClass<UInputComponent>();
	if (InputComponent)
	{
		if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(InputComponent))
		{
			return EnhancedInputComponent;
		}
	}
	return nullptr;
}

void UWaitForInputKeyReleaseAsyncAction::InternalTick(float DeltaTime)
{
	Super::InternalTick(DeltaTime);
	// UE_LOG(LogTemp, Warning, TEXT("%d: Tick!"), OwningAbility->AbilityComponent->GetOwner()->GetLocalRole());
	// If no InputAction was passed, this node completes immediately
	if (!OwningAbility->HasAuthority() && OwningAbility->InitialAbilityData.ActionInput == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("Attempted to use WaitForKeyRelease without setting ActionInput"));
		InternalClientCompleted();
	}

	if (bTaskCompleted)
	{		
		InternalCompleted(false);
	}
}

void UWaitForInputKeyReleaseAsyncAction::InternalCompleted(bool Forced)
{
	Super::InternalCompleted(Forced);
	
	if (InputReleaseBindHandle != -1)
	{
		GetEnhancedInputComponent()->RemoveBindingByHandle(InputReleaseBindHandle);
	}
}


