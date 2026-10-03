// Deprecated with ExecuteSyncedEvent (GMAS 1.4.1); removed in 1.5. FireCustomEvent carries a tag and an FInstancedStruct instead.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTags.h"

// Fix for instanced struct on previous 5.5 version
#if ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION >= 5
	#include "StructUtils/InstancedStruct.h"
#else
	#include "InstancedStruct.h"
#endif

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

	UPROPERTY(BlueprintReadWrite, Category = "GMASSyncedEvent")
	FGameplayTag EventTag;

	UPROPERTY(BlueprintReadWrite, Category = "GMASSyncedEvent")
	FInstancedStruct InstancedPayload;
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGMASSyncedEventData_AddImpulse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadWrite, Category = "Impulse")
	FVector Impulse {FVector::Zero()};

	UPROPERTY(BlueprintReadWrite, Category = "Impulse")
	bool bVelocityChange {false};
};