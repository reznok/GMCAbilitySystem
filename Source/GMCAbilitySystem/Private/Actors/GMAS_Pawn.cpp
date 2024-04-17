#include "Actors/GMAS_Pawn.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "Utility/GMASUtilities.h"


// Sets default values
AGMAS_Pawn::AGMAS_Pawn(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Set this pawn to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	CapsuleComponent = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Capsule"));
	SetRootComponent(CapsuleComponent);
	CapsuleComponent->SetCapsuleRadius(30.f);
	CapsuleComponent->SetCapsuleHalfHeight(90.f);
	CapsuleComponent->bEditableWhenInherited = true;
	CapsuleComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CapsuleComponent->SetCollisionObjectType(ECC_Pawn);
	CapsuleComponent->SetCollisionResponseToAllChannels(ECR_Block);
	CapsuleComponent->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	CapsuleComponent->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	
	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Skeletal Mesh"));
	MeshComponent->bEditableWhenInherited = true;
	MeshComponent->SetupAttachment(CapsuleComponent);
	MeshComponent->SetRelativeLocation(FVector(0.f, 0.f, -90.f));
	MeshComponent->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
	MeshComponent->SetCollisionProfileName(TEXT("NoCollision"));
	
	SpringArmComponent = CreateDefaultSubobject<USpringArmComponent>(TEXT("Spring Arm"));
	SpringArmComponent->bEditableWhenInherited = true;
	SpringArmComponent->SetupAttachment(CapsuleComponent);
	SpringArmComponent->TargetArmLength = 400.f;
	SpringArmComponent->bUsePawnControlRotation = true;

	CameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("Follow Camera"));
	CameraComponent->bEditableWhenInherited = true;
	CameraComponent->SetupAttachment(SpringArmComponent);
	CameraComponent->bUsePawnControlRotation = false;

	// Add our basic goodies.
	AbilitySystemComponent = CreateDefaultSubobject<UGMC_AbilitySystemComponent>(TEXT("Ability Component"));

#if WITH_EDITOR
	UGMASUtilities::SetPropertyFlagsSafe(StaticClass(), TEXT("CapsuleComponent"), CPF_DisableEditOnInstance);
	UGMASUtilities::SetPropertyFlagsSafe(StaticClass(), TEXT("MeshComponent"), CPF_DisableEditOnInstance);
	UGMASUtilities::SetPropertyFlagsSafe(StaticClass(), TEXT("SpringArmComponent"), CPF_DisableEditOnInstance);
	UGMASUtilities::SetPropertyFlagsSafe(StaticClass(), TEXT("CameraComponent"), CPF_DisableEditOnInstance);
	UGMASUtilities::SetPropertyFlagsSafe(StaticClass(), TEXT("AbilitySystemComponent"), CPF_DisableEditOnInstance);
#endif
}

// Called to bind functionality to input
void AGMAS_Pawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	if (const UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(PlayerInputComponent); InputMappingContext && EnhancedInput)
	{
		if (const AGMC_PlayerController* Player = Cast<AGMC_PlayerController>(GetController()))
		{
			if (Player->GetLocalPlayer() != nullptr)
			{
				if (UEnhancedInputLocalPlayerSubsystem* InputSystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Player->GetLocalPlayer()))
				{
					// Add our default system as the lowest mapping context.
					InputSystem->AddMappingContext(InputMappingContext, 0);
				}				
			}
		}
	}

	Super::SetupPlayerInputComponent(PlayerInputComponent);
}

