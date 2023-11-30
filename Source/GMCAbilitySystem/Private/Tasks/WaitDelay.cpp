#include "Tasks/WaitDelay.h"
#include "Components/GMCAbilityComponent.h"

void UWaitDelayAsyncAction::InternalTick(float DeltaTime)
{
	Super::InternalTick(DeltaTime);

	// UE_LOG(LogTemp, Warning, TEXT("Tick"));
	
	float TimePassed = GetWorld()->GetRealTimeSeconds() - StartTime;

	if (!bTaskCompleted && TimePassed >= Duration && !OwningAbility->HasAuthority())
	{
		InternalClientCompleted();
	}

	if (TimePassed >= Duration - FaultTolerance && bTaskCompleted)
	{		
		InternalCompleted(false);
	}
}


UWaitDelayAsyncAction* UWaitDelayAsyncAction::WaitForDelay(const UObject* WorldContext, UGMCAbility* Ability,
	float Duration)
{
	UWorld* ContextWorld = GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull);
	if(!ensureAlwaysMsgf(IsValid(WorldContext), TEXT("World Context was not valid.")))
	{
		return nullptr;
	}

	// Create a new UWaitDelayAsyncAction, and store function arguments in it.
	UWaitDelayAsyncAction* NewAction = NewObject<UWaitDelayAsyncAction>();
	NewAction->OwningAbility = Ability;
	NewAction->ContextWorld = ContextWorld;
	NewAction->Duration = Duration;
	NewAction->RegisterWithGameInstance(ContextWorld->GetGameInstance());
	return NewAction;
}

void UWaitDelayAsyncAction::Activate()
{
	Super::Activate();
	TickHandle = OwningAbility->AbilityComponent->OnFGMCAbilitySystemComponentTickDelegate.AddUObject(this, &UWaitDelayAsyncAction::InternalTick);
	StartTime = GetWorld()->GetRealTimeSeconds();
}
