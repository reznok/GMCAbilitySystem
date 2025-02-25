

// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Effects/GMCAbilityEffect.h"
#include "GMC_GameCueSet.generated.h"

USTRUCT()
struct FGameCueNotifyData
{
	GENERATED_USTRUCT_BODY()

	FGameCueNotifyData()
	: LoadedGameCueClass(nullptr)
	, ParentDataIdx( INDEX_NONE )
	{
	}

	UPROPERTY(EditAnywhere, Category=GameCue)
	FGameplayTag GameCueTag;

	UPROPERTY(EditAnywhere, Category=GameCue, meta=(AllowedClasses="/Script/GameplayAbilities.GameCueNotify_Static, /Script/GameplayAbilities.GameCueNotify_Actor"))
	FSoftObjectPath GameCueNotifyObj;

	UPROPERTY(transient)
	TObjectPtr<UClass> LoadedGameCueClass;

	int32 ParentDataIdx;
};

struct FGameCueReferencePair
{
	FGameplayTag GameCueTag;
	FSoftObjectPath StringRef;

	FGameCueReferencePair(const FGameplayTag& InGameCueTag, const FSoftObjectPath& InStringRef)
		: GameCueTag(InGameCueTag)
		, StringRef(InStringRef)
	{}
};

/**
 *	A set of gameplay cue actors to handle gameplay cue events
 */
UCLASS()
class GMCABILITYSYSTEM_API UGameCueSet : public UDataAsset
{
	GENERATED_UCLASS_BODY()

	/** Handles the cue event by spawning the cue actor. Returns true if the event was handled. */
	virtual bool HandleGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Adds a list of cues to the set */
	virtual void AddCues(const TArray<FGameCueReferencePair>& CuesToAdd);

	/** Removes all cues from the set matching any of the supplied tags */
	virtual void RemoveCuesByTags(const FGameplayTagContainer& TagsToRemove);

	/** Removes all cues from the set matching the supplied string refs */
	virtual void RemoveCuesByStringRefs(const TArray<FSoftObjectPath>& CuesToRemove);

	/** Nulls reference to the loaded class. Note this doesn't remove the entire cue from the internal data structure, just the hard ref to the loaded class */
	virtual void RemoveLoadedClass(UClass* Class);

	/** Returns filenames of everything we know about (loaded or not) */
	virtual void GetFilenames(TArray<FString>& Filenames) const;

	/** Extracts all soft object paths pointing to Cues */
	virtual void GetSoftObjectPaths(TArray<FSoftObjectPath>& List) const;

#if WITH_EDITOR

	void CopyCueDataToSetForEditorPreview(FGameplayTag Tag, UGameCueSet* DestinationSet);

	/** Updates an existing cue */
	virtual void UpdateCueByStringRefs(const FSoftObjectPath& CueToRemove, FString NewPath);

#endif

	/** Removes all cues from the set */
	virtual void Empty();

	virtual void PrintCues() const;
	
	UPROPERTY(EditAnywhere, Category=CueSet)
	TArray<FGameCueNotifyData> GameCueData;

	/** Maps GameCue Tag to index into above GameCues array. */
	TMap<FGameplayTag, int32> GameCueDataMap;

	static FGameplayTag	BaseGameCueTag();

protected:
	virtual bool HandleGameCueNotify_Internal(AActor* TargetActor, int32 DataIdx, EGameCueEvent::Type EventType, FGameCueParameters& Parameters);
	virtual void BuildAccelerationMap_Internal();
};
