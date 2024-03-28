#pragma once

#include "CoreMinimal.h"
#include "GMCPawn.h"
#include "Animation/AnimInstance.h"
#include "Utility/GameplayElementMapping.h"
#include "GMCAbilityAnimInstance.generated.h"

class UGMC_AbilitySystemComponent;
/**
 * An animation blueprint with built-in support for the GMC Ability System. A property will automatically be
 * set to the owner's ability system (if present), and a "Tag Property Map" allows any valid gameplay tag to be
 * mapped automatically to properties.
 */
UCLASS(ClassGroup="Animation", meta=(DisplayName="GMAS Animation Blueprint"))
class GMCABILITYSYSTEM_API UGMCAbilityAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UGMCAbilityAnimInstance(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	virtual void NativeBeginPlay() override;
	virtual void NativeInitializeAnimation() override;

	UGMC_AbilitySystemComponent* GetAbilitySystemComponent() const { return AbilitySystemComponent; }
	
protected:

#if WITH_EDITORONLY_DATA
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ability System")
	TSubclassOf<AGMC_Pawn> EditorPreviewClass { AGMC_Pawn::StaticClass() };
#endif
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, AdvancedDisplay, Category="Ability System", meta=(EditConditionHides))
	AGMC_Pawn* GMCPawn;
	
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, AdvancedDisplay, Category="Ability System", meta=(EditConditionHides))
	UGMC_AbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Ability System")
	FGMCGameplayElementTagPropertyMap TagPropertyMap;

	friend UGMC_AbilitySystemComponent;
};
