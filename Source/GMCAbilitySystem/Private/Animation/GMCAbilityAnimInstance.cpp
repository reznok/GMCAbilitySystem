#include "Animation/GMCAbilityAnimInstance.h"
#include "GMCAbilityComponent.h"

void UGMCAbilityAnimInstance::NativeInitializeAnimation()
{
	if (const AActor* OwnerActor = GetOwningActor())
	{
		AbilitySystemComponent = Cast<UGMC_AbilitySystemComponent>(OwnerActor->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
		if (AbilitySystemComponent)
		{
			TagPropertyMap.Initialize(this, AbilitySystemComponent);
		}
	}

	// As the super call will trigger the blueprint initialize event, we want to do it *after* we've set our
	// Ability System Component.
	Super::NativeInitializeAnimation();
}
