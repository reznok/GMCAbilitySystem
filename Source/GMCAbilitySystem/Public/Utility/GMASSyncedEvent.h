#pragma once

#include "GameplayTags.h"
#include "InstancedStruct.h"
#include "GMASSyncedEvent.generated.h"

UCLASS()
class GMCABILITYSYSTEM_API UGMASSyncedEvent : public UObject
{
	GENERATED_BODY()
};

UENUM()
enum EGMASSyncedEventType
{
	BlueprintImplemented,
	Custom,
	AddImpulse,
	PlayMontage
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASSyncedEventContainer
{
	GENERATED_BODY()

	UPROPERTY()
	TEnumAsByte<EGMASSyncedEventType> EventType{BlueprintImplemented};

	UPROPERTY(BlueprintReadWrite)
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadWrite)
	FInstancedStruct InstancedPayload;
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASSyncedEventData_AddImpulse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite)
	FVector Impulse {FVector::Zero()};

	UPROPERTY(BlueprintReadWrite)
	bool bVelocityChange {false};
};