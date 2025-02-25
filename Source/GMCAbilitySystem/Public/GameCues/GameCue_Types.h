// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameplayTagContainer.h"
#include "Effects/GMCAbilityEffectTypes.h"
#include "GameplayPrediction.h"
#include "UObject/ObjectKey.h"

#include "Effects/GMCAbilityEffect.h"
#include "GameCue_Types.generated.h"

class AGameCueNotify_Actor;
class UGMC_AbilitySystemComponent;
class UGameCueSet;


/** Describes what type of payload is attached to a cue execution, we only replicate what is needed */
UENUM()
enum class EGameCuePayloadType : uint8
{
	/** Uses FGameCueParameters */
	CueParameters,
	/** Uses FAbilityEffectSpecForRPC */
	FromSpec,
};


/** Structure to keep track of pending gameplay cues that haven't been applied yet. */
USTRUCT()
struct FGameCuePendingExecute
{
	GENERATED_USTRUCT_BODY()

	FGameCuePendingExecute() {}

	/** List of tags, we allocate one as there is almost always exactly one tag */
	TArray<FGameplayTag, TInlineAllocator<1> > GameCueTags;
	
	/** Prediction key that spawned this cue */
	UPROPERTY()
	FPredictionKey PredictionKey;

	/** What type of payload is attached to this cue */
	UPROPERTY()
	EGameCuePayloadType PayloadType = EGameCuePayloadType::CueParameters;

	/** What component to send the cue on */
	UPROPERTY()
	TObjectPtr<UGMC_AbilitySystemComponent> OwningComponent = nullptr;

	/** If this cue is from a spec, here's the copy of that spec */
	UPROPERTY()
	FAbilityEffectSpecForRPC FromSpec;

	/** Store the full cue parameters or just the effect context depending on type */
	UPROPERTY()
	FGameCueParameters CueParameters;
};

USTRUCT()
struct FGameCueNotifyActorArray
{
	GENERATED_BODY()

	UPROPERTY(transient)
	TArray<TObjectPtr<AGameCueNotify_Actor>> Actors;
};




/** Struct for pooling and preallocating GameCuenotify_actor classes. This data is per world and used to track what actors are available to recycle and which classes need to preallocate instances of those actors */
USTRUCT()
struct FPreallocationInfo
{
	GENERATED_USTRUCT_BODY()

	/** Raw list of pooled instances. This relies on NotifyGameCueActorEndPlay always being called when actor is destroyed */
	UPROPERTY(transient)
	TMap<TObjectPtr<UClass>, FGameCueNotifyActorArray> PreallocatedInstances;

	/** List of classes that will be pooled */
	UPROPERTY(transient)
	TArray<TSubclassOf<AGameCueNotify_Actor>> ClassesNeedingPreallocation;

	/** World that owns this list */
	FObjectKey OwningWorldKey;
};


/** Struct that is used by the GameCue manager to tie an instanced GameCue to the calling gamecode. Usually this is just the target actor, but can also be unique per instigator/sourceobject */
struct UE_DEPRECATED(5.3, "Look at FGCNotifyActorKey constructor notes for upgrade steps.") FGCNotifyActorKey
{
	FGCNotifyActorKey()
	{

	}

	// This class is deprecated.  It was previously used to index into a map to find an instance of AGameCueNotify_Actor which was in use.
	// Instead, you can find the AGameCueNotify_Actor this way:
	//	1. InTargetActor will be its parent.  So you search InTargetActor->Children for instances of AGameCueNotify_Actor.
	//	2. InCueClass can be inferred from AGameCueNotify_Actor->GetClass().
	//	3. AGameCueNotify_Actor has two new fields: CueInstigator (corresponds to InInstigatorActor) and CueSourceObject; (corresponds to InSourceObj).
	// Refer to UGameCueManager::GetInstancedCueActor.
	FGCNotifyActorKey(AActor* InTargetActor, UClass* InCueClass, AActor* InInstigatorActor=nullptr, const UObject* InSourceObj=nullptr)
	{
		TargetActor = FObjectKey(InTargetActor);
		OptionalInstigatorActor = FObjectKey(InInstigatorActor);
		OptionalSourceObject = FObjectKey(InSourceObj);
		CueClass = FObjectKey(InCueClass);
	}

	

	FObjectKey	TargetActor;
	FObjectKey	OptionalInstigatorActor;
	FObjectKey	OptionalSourceObject;
	FObjectKey	CueClass;

	FORCEINLINE bool operator==(const FGCNotifyActorKey& Other) const
	{
		return TargetActor == Other.TargetActor && CueClass == Other.CueClass &&
				OptionalInstigatorActor == Other.OptionalInstigatorActor && OptionalSourceObject == Other.OptionalSourceObject;
	}
};

PRAGMA_DISABLE_DEPRECATION_WARNINGS
FORCEINLINE uint32 GetTypeHash(const FGCNotifyActorKey& Key)
{
	return GetTypeHash(Key.TargetActor)	^
			GetTypeHash(Key.OptionalInstigatorActor) ^
			GetTypeHash(Key.OptionalSourceObject) ^
			GetTypeHash(Key.CueClass);
}
PRAGMA_ENABLE_DEPRECATION_WARNINGS

/**
 *	FScopedGameCueSendContext
 *	Add this around code that sends multiple gameplay cues to allow grouping them into a smalkler number of cues for more efficient networking
 */
struct GMCABILITYSYSTEM_API FScopedGameCueSendContext
{
	FScopedGameCueSendContext();
	~FScopedGameCueSendContext();
};

/** Delegate for when GC notifies are added or removed from manager */
DECLARE_MULTICAST_DELEGATE(FOnGameCueNotifyChange);