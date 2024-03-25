#include "Animation/GMCAbilityAnimInstance.h"
#include "GMCAbilityComponent.h"

void UGMCAbilityAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	bool bShouldInitializeProperties = true;
	
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
				GMCPawn = EditorPreviewClass->GetDefaultObject<AGMC_Pawn>();

				// Since we might have overridden the editor preview class with something that already has an ability component,
				// try getting the ability component again.
				AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(GMCPawn->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
			}
			
			if (!IsValid(AbilitySystemComponent))
			{
				// There's not going to be any attributes or tags really to work with when using the
				// default parent class.
				bShouldInitializeProperties = false;
			
				AbilitySystemComponent = NewObject<UGMC_AbilitySystemComponent>();
			}
		}
#endif
	}

	if (AbilitySystemComponent)
	{
		if (bShouldInitializeProperties)
		{
			TagPropertyMap.Initialize(this, AbilitySystemComponent);
		}
		else
		{
			UE_LOG(LogGMCAbilitySystem, Log, TEXT("%s: skipping property map initialization since we're an in-editor preview; we're using the default ability system component, and won't have valid data."),
				*GetClass()->GetName());
		}
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

