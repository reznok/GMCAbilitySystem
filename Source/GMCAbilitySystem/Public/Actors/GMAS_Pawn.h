#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityComponent.h"
#include "GMCPawn.h"
#include "InputMappingContext.h"
#include "GMAS_Pawn.generated.h"

class UCameraComponent;
class USpringArmComponent;


/**
 * The GMAS Pawn is a generic pawn which contains a pre-set-up capsule, spring arm, camera,
 * and so on, as well as an ability system component. It will also automatically set up the input mapping context
 * if one is provided. This pawn is *purely* a convenience class; it is not required to use anything in GMAS.
 * 
 * A movement component is *not* pre-set-up, because people will almost always want to use their own subclass,
 * and that would be difficult to override in Blueprints.
 */
UCLASS(ClassGroup=(GMAS), meta=(DisplayName="GMAS Pawn"))
class GMCABILITYSYSTEM_API AGMAS_Pawn : public AGMC_Pawn
{
	GENERATED_BODY()

public:
	// Sets default values for this pawn's properties
	AGMAS_Pawn(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

public:

	// Called to bind functionality to input
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

protected:

	// A convenience setting; if this is set, the given input mapping context will be added with
	// priority 0 automatically.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Input")
	TObjectPtr<UInputMappingContext> InputMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components", DisplayName="Capsule", meta=(AllowPrivateAccess=true))
	TObjectPtr<UCapsuleComponent> CapsuleComponent;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components", DisplayName="Skeletal Mesh", meta=(AllowPrivateAccess=true))
	TObjectPtr<USkeletalMeshComponent> MeshComponent;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components", DisplayName="Spring Arm", meta=(AllowPrivateAccess=true))
	TObjectPtr<USpringArmComponent> SpringArmComponent;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Components", DisplayName="Follow Camera", meta=(AllowPrivateAccess=true))
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Components")
	TObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;

};
