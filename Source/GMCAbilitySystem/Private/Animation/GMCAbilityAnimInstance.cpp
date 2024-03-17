#include "Animation/GMCAbilityAnimInstance.h"
#include "GMCPawn.h"
#include "GMCAbilityComponent.h"

void UGMCAbilityAnimInstance::NativeInitializeAnimation()
{
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
		if (!GetWorld()->IsGameWorld() && !IsValid(AbilitySystemComponent) && GMCPawn)
		{
			// Create a default for in-editor preview.
			AbilitySystemComponent = GMCPawn->CreateDefaultSubobject<UGMC_AbilitySystemComponent>(TEXT("AbilityComponent"));
		}
#endif
	}

	if (AbilitySystemComponent)
	{
		TagPropertyMap.Initialize(this, AbilitySystemComponent);
	}
	
	// As the super call will trigger the blueprint initialize event, we want to do it *after* we've set our
	// Ability System Component.
	Super::NativeInitializeAnimation();
}
