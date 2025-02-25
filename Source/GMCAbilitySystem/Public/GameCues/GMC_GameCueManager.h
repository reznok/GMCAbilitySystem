// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMC_GameCueSet.h"
#include "Engine/DataAsset.h"
#include "GameCues/GameCue_Types.h"
#include "GameCues/GameCueNotify_Actor.h"
#include "GameCues/GameCueNotify_Static.h"
#include "Engine/StreamableManager.h"
#include "GMC_GameCueManager.generated.h"

/**
 * 
 */

class AGameCueNotify_Actor;
class UGMC_AbilitySystemComponent;
class UObjectLibrary;

DECLARE_DELEGATE_OneParam(FOnGameCueNotifySetLoaded, TArray<FSoftObjectPath>);
DECLARE_DELEGATE_OneParam(FGameCueProxyTick, float);
DECLARE_DELEGATE_RetVal_TwoParams(bool, FShouldLoadGCNotifyDelegate, const FAssetData&, FName);


enum class EGameCueExecutionOptions : int32
{
	// Default options, check everything
	Default = 0,
	// Skip gameplay cue interface check
	IgnoreInterfaces	= 0x00000001,
	// Skip spawning notifies
	IgnoreNotifies		= 0x00000002,
	// Skip tag translation step
	IgnoreTranslation	= 0x00000004,
	// Ignores suppression check, always spawns
	IgnoreSuppression	= 0x00000008,
	// Don't show debug visualizations
	IgnoreDebug			= 0x00000010
};
ENUM_CLASS_FLAGS(EGameCueExecutionOptions);

/** An ObjectLibrary for the GameCue Notifies. Wraps 2 underlying UObjectLibraries plus options/delegates for how they are loaded */ 
USTRUCT()
struct FGameCueObjectLibrary
{
	GENERATED_BODY()
	FGameCueObjectLibrary()
	: ActorObjectLibrary(nullptr)
			, StaticObjectLibrary(nullptr)
			, CueSet(nullptr)
			, AsyncPriority(0)
			, bShouldSyncScan(false)
			, bShouldAsyncLoad(false)
			, bShouldSyncLoad(false)
			, bHasBeenInitialized(false)
	{
	}

	/** Paths to search for */
	UPROPERTY()
	TArray<FString> Paths;

	/** Callback for when load finishes */
	FOnGameCueNotifySetLoaded OnLoaded;

	/** Callback for "should I add this FAssetData to the set" */
	FShouldLoadGCNotifyDelegate ShouldLoad;

	/** Object library for actor based notifies */
	UPROPERTY()
	TObjectPtr<UObjectLibrary> ActorObjectLibrary;

	/** Object library for object based notifies */
	UPROPERTY()
	TObjectPtr<UObjectLibrary> StaticObjectLibrary;

	/** Set to put the loaded asset data into. If null we will use the global set (RuntimeGameCueObjectLibrary.CueSet) */
	UPROPERTY()
	TObjectPtr<UGameCueSet> CueSet;

	/** Priority to use if async loading */
	TAsyncLoadPriority AsyncPriority;

	/** Should we force a sync scan on the asset registry in order to discover asset data, or just use what is there */
	UPROPERTY()
	bool bShouldSyncScan;

	/** Should we start async loading everything that we find (that passes ShouldLoad delegate check) */
	UPROPERTY()
	bool bShouldAsyncLoad;

	/** Should we sync load everything that we find (that passes ShouldLoad delegate check) */
	UPROPERTY()
	bool bShouldSyncLoad;

	/** True if this has been initialized with correct data */
	UPROPERTY()
	bool bHasBeenInitialized;
};

UCLASS()
class GMCABILITYSYSTEM_API UGameCueManager : public UDataAsset
{
	GENERATED_BODY()

public:
	UGameCueManager(const FObjectInitializer& ObjectInitializer);
 
	FStreamableManager	StreamableManager;

	static bool IsGameCueRecylingEnabled();

	/** Called when manager is first created */
	virtual void OnCreated();
	void OnPostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	bool ShouldSyncLoadMissingGameCues() const;
	bool ShouldAsyncLoadMissingGameCues() const;
	virtual bool HandleMissingGameCue(UGameCueSet* OwningSet, struct FGameCueNotifyData& CueData, AActor* TargetActor, EGameCueEvent::Type EventType, FGameCueParameters& Parameters);
	void OnMissingCueAsyncLoadComplete(FSoftObjectPath LoadedObject, TWeakObjectPtr<UGameCueSet> OwningSet,
	                                   FGameplayTag GameCueTag, TWeakObjectPtr<AActor> TargetActor,
	                                   EGameCueEvent::Type EventType, FGameCueParameters Parameters);
	void OnEngineInitComplete();
	/** Handles updating an object library when a new asset is created */
	void HandleAssetAdded(UObject *Object);
	virtual bool ShouldAsyncLoadObjectLibrariesAtStart() const { return true; }
	/** Handles cleaning up an object library if it matches the passed in object */
	void HandleAssetDeleted(UObject *Object);

	/** Called to setup and initialize the runtime library. The passed in paths will be scanned and added to the global gameplay cue set where appropriate */
	void InitializeRuntimeObjectLibrary();
	static UWorld* GetCachedWorldForGameCueNotifies();

	TArray<UGameCueSet*> GetGlobalCueSets();
	UGameCueSet* GetRuntimeCueSet();

	DECLARE_EVENT_FiveParams(UGameCueManager, FOnRouteGameCue, AActor*, FGameplayTag, EGameCueEvent::Type, const FGameCueParameters&, EGameCueExecutionOptions);
	FOnRouteGameCue& OnGameCueRouted() { return OnRouteGameCue; }

	
	/** Force any instanced GameCueNotifies to stop */
	virtual void EndGameCuesFor(AActor* TargetActor);

	/** Returns the cached instance cue. Creates it if it doesn't exist. */
	virtual AGameCueNotify_Actor* GetInstancedCueActor(AActor* TargetActor, UClass* GameCueNotifyActorClass, const FGameCueParameters& Parameters);
	AGameCueNotify_Actor* FindRecycledCue(const TSubclassOf<AGameCueNotify_Actor>& CueClass, const UWorld& FindInWorld);
	FPreallocationInfo& GetPreallocationInfo(const UWorld* World);
	AGameCueNotify_Actor* FindExistingCueOnActor(const AActor& TargetActor,
	                                             const TSubclassOf<AGameCueNotify_Actor>& CueClass,
	                                             const FGameCueParameters& Parameters) const;

	/** Notify that this actor is finished and should be destroyed or recycled */
	virtual void NotifyGameCueActorFinished(AGameCueNotify_Actor* Actor);

	/** Notify to say the actor is about to be destroyed and the GC manager needs to remove references to it. This should not happen in normal play with recycling enabled, but could happen in replays. */
	virtual void NotifyGameCueActorEndPlay(AGameCueNotify_Actor* Actor);

	UPROPERTY(transient)
	TArray<FPreallocationInfo>	PreallocationInfoList_Internal;


	// -------------------------------------------------------------
	// Handling GameCues at runtime:
	// -------------------------------------------------------------

	/** Main entry point for handling a GameCue event. These functions will call the 3 functions below to handle gameplay cues */
	virtual void HandleGameCues(AActor* TargetActor, const FGameplayTagContainer& GameCueTags, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters, EGameCueExecutionOptions Options = EGameCueExecutionOptions::Default);
	virtual void HandleGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters, EGameCueExecutionOptions Options = EGameCueExecutionOptions::Default);

	/** 1. returns true to ignore gameplay cues */
	virtual bool ShouldSuppressGameCues(AActor* TargetActor);

	/** 2. Allows Tag to be translated in place to a different Tag. See FGameCueTranslorManager */
	void TranslateGameCue(FGameplayTag& Tag, AActor* TargetActor, const FGameCueParameters& Parameters);

	/** 3. Actually routes the GameCue event to the right place.  */
	virtual void RouteGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters, EGameCueExecutionOptions Options = EGameCueExecutionOptions::Default);


protected:
	/** Internal function to actually init the FGameCueObjectLibrary.  Returns StreamableHandle when asset async loading is requested. */
	TSharedPtr<FStreamableHandle> InitObjectLibrary(FGameCueObjectLibrary& Library);

	virtual TArray<FString> GetAlwaysLoadedGameCuePaths();

	/** returns list of valid gameplay cue paths. Subclasses may override this to specify locations that aren't part of the "always loaded" LoadedPaths array */
	virtual TArray<FString>	GetValidGameCuePaths() { return GetAlwaysLoadedGameCuePaths(); }
	
	UPROPERTY(transient)
	FGameCueObjectLibrary RuntimeGameCueObjectLibrary;

	UPROPERTY(transient)
	FGameCueObjectLibrary EditorGameCueObjectLibrary;

	/** Handle to maintain ownership of gameplay cue assets.  
	  Note:	Only the latest handle to async load request is cached.
			If projects require multiple concurrent asynchronous loads, handles returned from InitObjectLibrary should be cached as needed. */
	TSharedPtr<FStreamableHandle> GameCueAssetHandle;


#if WITH_EDITOR

	/** Called from editor to soft load all gameplay cue notifies for the GameCueEditor */
	void InitializeEditorObjectLibrary();


	/** Calling this will make the GC manager periodically refresh the EditorObjectLibrary until the asset registry is finished scanning */
	void RequestPeriodicUpdateOfEditorObjectLibraryWhileWaitingOnAssetRegistry();


	FOnGameCueNotifyChange	OnGameCueNotifyAddOrRemove;
	bool bAccelerationMapOutdated;

	/** Warns if we move a GameCue notify out of the valid search paths */
	void HandleAssetRenamed(const FAssetData& Data, const FString& String);
	bool VerifyNotifyAssetIsInValidPath(FString Path);

	//This handles the case where GameCueNotifications have changed between sessions, which is possible in editor.
	virtual void ReloadObjectLibrary(UWorld* World, const UWorld::InitializationValues IVS);
	void RefreshObjectLibraries();

	UGameCueSet* GetEditorCueSet();

	FSimpleMulticastDelegate OnEditorObjectLibraryUpdated;
	bool EditorObjectLibraryFullyInitialized;


	FTimerHandle EditorPeriodicUpdateHandle;
	/** Animation Preview Hacks */
	static class USceneComponent* PreviewComponent;
	static UWorld* PreviewWorld;
	static FGameCueProxyTick PreviewProxyTick;
#endif

	FOnRouteGameCue OnRouteGameCue;

	/** Cached world we are currently handling cues for. Used for non instanced GC Notifies that need world. */
	static UWorld* CurrentWorld;
	//FGameCueTranslationManager	TranslationManager;

	
	virtual bool ShouldSyncScanRuntimeObjectLibraries() const;
	virtual bool ShouldSyncLoadRuntimeObjectLibraries() const;
	virtual bool ShouldAsyncLoadRuntimeObjectLibraries() const;

	void BuildCuesToAddToGlobalSet(const TArray<FAssetData>& AssetDataList, FName TagPropertyName, TArray<struct FGameCueReferencePair>& OutCuesToAdd, TArray<FSoftObjectPath>& OutAssetsToLoad, FShouldLoadGCNotifyDelegate = FShouldLoadGCNotifyDelegate());
	/** Called before loading any gameplay cue notifies from object libraries. Allows subclasses to skip notifies. */
	virtual bool ShouldLoadGameCueAssetData(const FAssetData& Data) const { return true; }

};
