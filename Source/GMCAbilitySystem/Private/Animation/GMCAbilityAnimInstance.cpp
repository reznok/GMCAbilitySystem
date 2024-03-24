#include "Animation/GMCAbilityAnimInstance.h"
#include "GMCPawn.h"
#include "GMCAbilityComponent.h"

void UGMCAbilityAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();
	
	// A subclass may have already set these before calling us, so check if we need to do the work.
	if (!AbilitySystemComponent || !GMCPawn)
	{
		AActor* OwnerActor = GetOwningActor();
		if (OwnerActor)
		{
			GMCPawn = Cast<AGMC_Pawn>(OwnerActor);
			AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(OwnerActor->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
		}
		
#if WITH_EDITOR
		if (!GetWorld()->IsGameWorld() && !IsValid(AbilitySystemComponent) || !IsValid(GMCPawn))
		{
			// Create a default for in-editor preview.
			if (!IsValid(GMCPawn))
			{
				GMCPawn = GetMutableDefault<AGMC_Pawn>();
			}
			if (!IsValid(AbilitySystemComponent))
			{
				AbilitySystemComponent = NewObject<UGMC_AbilitySystemComponent>();
			}
		}
#endif
	}

	if (AbilitySystemComponent)
	{
		TagPropertyMap.Initialize(this, AbilitySystemComponent);
	}
	else
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("%s: unable to find a GMC Ability System Component on %s after initializing animation blueprint. Will make a last-ditch effort in BeginPlay."),
			*GetClass()->GetName(), *GetOwningActor()->GetName())
	}
}

void UGMCAbilityAnimInstance::NativeBeginPlay()
{
	// One last sanity-check, in case folks are adding things at runtime.
	if (GMCPawn && !AbilitySystemComponent)
	{
		AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(GMCPawn->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
		if (!AbilitySystemComponent)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("%s: last-ditch effort to find a GMC Ability System Component on %s failed!"), *GetClass()->GetPathName(), *GMCPawn->GetName());
			return;
		}

		TagPropertyMap.Initialize(this, AbilitySystemComponent);
	}

	Super::NativeBeginPlay();
}

