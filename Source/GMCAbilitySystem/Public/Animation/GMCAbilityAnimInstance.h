#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Utility/GameplayElementMapping.h"
#include "GMCAbilityAnimInstance.generated.h"

class UGMC_AbilitySystemComponent;
/**
 * An animation blueprint with built-in support for the GMC Ability System. A property will automatically be
 * set to the owner's ability system (if present), and a "Tag Property Map" allows any valid gameplay tag to be
 * mapped automatically to properties.
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	virtual void NativeBeginPlay() override;
	virtual void NativeInitializeAnimation() override;

	UGMC_AbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystemComponent; }
	
protected:

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Ability System")
	AGMC_Pawn* GMCPawn;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Ability System")
	UGMC_AbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ability System")
	FGMCGameplayElementTagPropertyMap TagPropertyMap;
};
