#include "Animation/GMCAbilityAnimInstance.h"
#include "GMCAbilityComponent.h"
#include "Utility/GMASUtilities.h"

UGMCAbilityAnimInstance::UGMCAbilityAnimInstance(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
#if WITH_EDITOR
	// Hide instance-only variables which we do not need cluttering everything up.
	UGMASUtilities::ClearPropertyFlagsSafe(StaticClass(), TEXT("GMCPawn"), CPF_SimpleDisplay | CPF_Edit);
	UGMASUtilities::ClearPropertyFlagsSafe(StaticClass(), TEXT("AbilitySystemComponent"), CPF_SimpleDisplay | CPF_Edit);
#endif
}

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
		if (!GetWorld()->IsGameWorld() && (!IsValid(AbilitySystemComponent) || !IsValid(GMCPawn)))
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
#if WITH_EDITOR
		if (!GetWorld()->IsGameWorld())
		{
			// We're in the editor preview; manually instantiate our attributes for purposes of property mapping
			// the default values. This is a convenience for debugging in-editor, so that the editor preview's
			// mapped properties should match the class-level defaults for the chosen editor preview class.
			AbilitySystemComponent->InstantiateAttributes();
			AbilitySystemComponent->SetStartingTags();
		}
#endif

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
		// Don't bother with this log in editor previews.
		if (GetWorld()->IsGameWorld())
		{
			UE_LOG(LogGMCAbilitySystem, Warning, TEXT("%s: unable to find a GMC Ability System Component on %s after initializing animation blueprint. Will make a last-ditch effort in BeginPlay."),
				*GetClass()->GetName(), *GetOwningActor()->GetName())
		}
	}
}

void UGMCAbilityAnimInstance::NativeBeginPlay()
{
	Super::NativeBeginPlay();
	
	// Components added in blueprint won't be available during InitializeAnimation but will by the time we hit BeginPlay;
	// make a second attempt to set up our property bindings in that scenario.
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
}

