// Fill out your copyright notice in the Description page of Project Settings.


#include "GameCues/GMC_GameCueManager.h"
#include "GMCAbilitySystemGlobals.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/ObjectLibrary.h"
#include "GameCues/GMC_GameCueInterface.h"


#if WITH_EDITOR
#include "Editor.h"
#include "Engine/Engine.h"

USceneComponent* UGameCueManager::PreviewComponent = nullptr;
UWorld* UGameCueManager::PreviewWorld = nullptr;
FGameCueProxyTick UGameCueManager::PreviewProxyTick;
#define LOCTEXT_NAMESPACE "GameCueManager"

#endif
int32 DisplayGameCues = 0;
static FAutoConsoleVariableRef CVarDisplayGameCues(TEXT("AbilitySystem.DisplayGameCues"),	DisplayGameCues, TEXT("Display GameCue events in world as text."), ECVF_Default	);

int32 DisableGameCues = 0;
static FAutoConsoleVariableRef CVarDisableGameCues(TEXT("AbilitySystem.DisableGameCues"),	DisableGameCues, TEXT("Disables all GameCue events in the world."), ECVF_Default );
float DisplayGameCueDuration = 5.f;
static FAutoConsoleVariableRef CVarDurationeGameCues(TEXT("AbilitySystem.GameCue.DisplayDuration"),	DisplayGameCueDuration, TEXT("Disables all GameCue events in the world."), ECVF_Default );

int32 GameCueRunOnDedicatedServer = 0;
static FAutoConsoleVariableRef CVarDedicatedServerGameCues(TEXT("AbilitySystem.GameCue.RunOnDedicatedServer"), GameCueRunOnDedicatedServer, TEXT("Run gameplay cue events on dedicated server"), ECVF_Default );

UGameCueManager::UGameCueManager(const FObjectInitializer& PCIP)
	: Super(PCIP)
{
#if WITH_EDITOR
	bAccelerationMapOutdated = true;
	EditorObjectLibraryFullyInitialized = false;
#endif
}

bool IsDedicatedServerForGameCue()
{
#if WITH_EDITOR
	// This will handle dedicated server PIE case properly
	return GEngine->ShouldAbsorbCosmeticOnlyEvent();
#else
	// When in standalone non editor, this is the fastest way to check
	return IsRunningDedicatedServer();
#endif
}
UWorld* UGameCueManager::CurrentWorld = nullptr;

void UGameCueManager::OnCreated()
{
	FWorldDelegates::OnPostWorldCleanup.AddUObject(this, &UGameCueManager::OnPostWorldCleanup);
	//FNetworkReplayDelegates::OnPreScrub.AddUObject(this, &UGameCueManager::OnPreReplayScrub);


#if WITH_EDITOR
	if (GIsRunning)
	{
		// Engine init already completed
		OnEngineInitComplete();

	}
	else
	{
		FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &UGameCueManager::OnEngineInitComplete);
	}
#endif
}


void UGameCueManager::OnPostWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	const FObjectKey WorldObjectKey(World);

	for (int32 idx=0; idx < PreallocationInfoList_Internal.Num(); ++idx)
	{
		FPreallocationInfo& PreallocationInfo = PreallocationInfoList_Internal[idx];
		if (PreallocationInfo.OwningWorldKey != WorldObjectKey)
		{
			continue;
		}
		
		UE_LOG(LogGMCAbilitySystem,Warning, TEXT("UGameCueManager::OnPostWorldCleanup %s Removing PreallocationInfoList_Internal element %d"), *GetNameSafe(World), idx);

		// Spit out some debug information to help us track down memory issues
		constexpr bool bWarnOnActiveActors = true;
	//	DumpPreallocationStats(PreallocationInfo, bWarnOnActiveActors);

		// Actually remove the entry which can contain hard references
		PreallocationInfoList_Internal.RemoveAtSwap(idx, 1, EAllowShrinking::No);
		idx--;
	}

	IGameCueInterface::ClearTagToFunctionMap();
}


bool UGameCueManager::HandleMissingGameCue(UGameCueSet* OwningSet, struct FGameCueNotifyData& CueData, AActor* TargetActor, EGameCueEvent::Type EventType, FGameCueParameters& Parameters)
{
	if (ShouldSyncLoadMissingGameCues())
	{
		CueData.LoadedGameCueClass = Cast<UClass>(StreamableManager.LoadSynchronous(CueData.GameCueNotifyObj, false));

		if (CueData.LoadedGameCueClass)
		{
			UE_LOG(LogTemp,Warning, TEXT("GameCueNotify %s was not loaded when GameCue was invoked, did synchronous load."), *CueData.GameCueNotifyObj.ToString());
			return true;
		}
		else
		{
				UE_LOG(LogTemp,Warning, TEXT("Late load of GameCueNotify %s failed!"), *CueData.GameCueNotifyObj.ToString());
		}
	}
	else if (ShouldAsyncLoadMissingGameCues())
	{
		// Not loaded: start async loading and call when loaded
		StreamableManager.RequestAsyncLoad(CueData.GameCueNotifyObj, FStreamableDelegate::CreateUObject(this, &UGameCueManager::OnMissingCueAsyncLoadComplete, 
			CueData.GameCueNotifyObj, TWeakObjectPtr<UGameCueSet>(OwningSet), CueData.GameCueTag, MakeWeakObjectPtr(TargetActor), EventType, Parameters));

		UE_LOG(LogTemp,Warning, TEXT("GameCueNotify %s was not loaded when GameCue was invoked. Starting async loading."), *CueData.GameCueNotifyObj.ToString());
	}
	return false;
}

void UGameCueManager::OnMissingCueAsyncLoadComplete(FSoftObjectPath LoadedObject, TWeakObjectPtr<UGameCueSet> OwningSet, FGameplayTag GameCueTag, TWeakObjectPtr<AActor> TargetActor, EGameCueEvent::Type EventType, FGameCueParameters Parameters)
{
	if (!LoadedObject.ResolveObject())
	{
		// Load failed
		UE_LOG(LogTemp,Warning, TEXT("Late load of GameCueNotify %s failed!"), *LoadedObject.ToString());
		return;
	}

	if (OwningSet.IsValid())
	{
		CurrentWorld = TargetActor.IsValid() ? TargetActor->GetWorld() : nullptr;
		if (!CurrentWorld)
		{
			// TargetActor has since been destroyed.  Attempt to get the world from the other actors.
			const AActor* CueInstigator = Parameters.GetInstigator();
			CurrentWorld = CueInstigator ? CueInstigator->GetWorld() : nullptr;
			if (!CurrentWorld)
			{
				const AActor* EffectCauser = Parameters.GetEffectCauser();
				CurrentWorld = EffectCauser ? EffectCauser->GetWorld() : nullptr;
			}
		}

		// Don't handle gameplay cues when world is tearing down
		if (!GetWorld() || GetWorld()->bIsTearingDown)
		{
			return;
		}

		// Objects are still valid, re-execute cue
		OwningSet->HandleGameCue(TargetActor.Get(), GameCueTag, EventType, Parameters);

		CurrentWorld = nullptr;
	}
}


bool UGameCueManager::ShouldSyncLoadMissingGameCues() const
{
	return false;
}

bool UGameCueManager::ShouldAsyncLoadMissingGameCues() const
{
	return true;
}

void UGameCueManager::OnEngineInitComplete()
{
#if WITH_EDITOR
	FCoreDelegates::OnFEngineLoopInitComplete.AddUObject(this, &UGameCueManager::OnEngineInitComplete);
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	AssetRegistryModule.Get().OnInMemoryAssetCreated().AddUObject(this, &UGameCueManager::HandleAssetAdded);
	AssetRegistryModule.Get().OnInMemoryAssetDeleted().AddUObject(this, &UGameCueManager::HandleAssetDeleted);
	AssetRegistryModule.Get().OnAssetRenamed().AddUObject(this, &UGameCueManager::HandleAssetRenamed);
	FWorldDelegates::OnPreWorldInitialization.AddUObject(this, &UGameCueManager::ReloadObjectLibrary);

	InitializeEditorObjectLibrary();
#endif
}



#if WITH_EDITOR
void UGameCueManager::InitializeEditorObjectLibrary()
{
	SCOPE_LOG_TIME_IN_SECONDS(*FString::Printf(TEXT("UGameCueManager::InitializeEditorObjectLibrary")), nullptr)

	EditorGameCueObjectLibrary.Paths = GetValidGameCuePaths();
	if (EditorGameCueObjectLibrary.CueSet == nullptr)
	{
		EditorGameCueObjectLibrary.CueSet = NewObject<UGameCueSet>(this, TEXT("EditorGameCueSet"));
	}

	EditorGameCueObjectLibrary.CueSet->Empty();
	EditorGameCueObjectLibrary.bHasBeenInitialized = true;

	// Don't load anything for the editor. Just read whatever the asset registry has.
	EditorGameCueObjectLibrary.bShouldSyncScan = IsRunningCommandlet();				// If we are cooking, then sync scan it right away so that we don't miss anything
	EditorGameCueObjectLibrary.bShouldAsyncLoad = false;
	EditorGameCueObjectLibrary.bShouldSyncLoad = false;

	InitObjectLibrary(EditorGameCueObjectLibrary);
	
	const FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	if ( AssetRegistryModule.Get().IsLoadingAssets() )
	{
		// Let us know when we are done
		static FDelegateHandle DoOnce =
		AssetRegistryModule.Get().OnFilesLoaded().AddUObject(this, &UGameCueManager::InitializeEditorObjectLibrary);
	}
	else
	{
		EditorObjectLibraryFullyInitialized = true;
		if (EditorPeriodicUpdateHandle.IsValid())
		{
			GEditor->GetTimerManager()->ClearTimer(EditorPeriodicUpdateHandle);
			EditorPeriodicUpdateHandle.Invalidate();
		}
	}

	OnEditorObjectLibraryUpdated.Broadcast();
}

void UGameCueManager::RequestPeriodicUpdateOfEditorObjectLibraryWhileWaitingOnAssetRegistry()
{	// Asset registry is still loading, so update every 15 seconds until its finished
	if (!EditorObjectLibraryFullyInitialized && !EditorPeriodicUpdateHandle.IsValid())
	{
		GEditor->GetTimerManager()->SetTimer( EditorPeriodicUpdateHandle, FTimerDelegate::CreateUObject(this, &UGameCueManager::InitializeEditorObjectLibrary), 15.f, true);
	}
}

void UGameCueManager::HandleAssetAdded(UObject* Object)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		UGameCueNotify_Static* StaticCDO = Cast<UGameCueNotify_Static>(Blueprint->GeneratedClass->ClassDefaultObject);
		AGameCueNotify_Actor* ActorCDO = Cast<AGameCueNotify_Actor>(Blueprint->GeneratedClass->ClassDefaultObject);
		
		if (StaticCDO || ActorCDO)
		{
			if (!Blueprint->GetOutermost()->HasAnyPackageFlags(PKG_ForDiffing) && VerifyNotifyAssetIsInValidPath(Blueprint->GetOuter()->GetPathName()))
			{
				FSoftObjectPath StringRef;
				StringRef.SetPath(Blueprint->GeneratedClass->GetPathName());

				TArray<FGameCueReferencePair> CuesToAdd;
				if (StaticCDO)
				{
					CuesToAdd.Add(FGameCueReferencePair(StaticCDO->GameCueTag, StringRef));
				}
				else if (ActorCDO)
				{
					CuesToAdd.Add(FGameCueReferencePair(ActorCDO->GameCueTag, StringRef));
				}

				// Make sure core knows about this ref so it can be properly detected during cook.
				StringRef.PostLoadPath(Object->GetLinker());

				for (UGameCueSet* Set : GetGlobalCueSets())
				{
					Set->AddCues(CuesToAdd);
				}

				OnGameCueNotifyAddOrRemove.Broadcast();
			}
		}
	}
}

void UGameCueManager::HandleAssetDeleted(UObject* Object)
{
	FSoftObjectPath StringRefToRemove;
	UBlueprint* Blueprint = Cast<UBlueprint>(Object);
	if (Blueprint && Blueprint->GeneratedClass)
	{
		UGameCueNotify_Static* StaticCDO = Cast<UGameCueNotify_Static>(Blueprint->GeneratedClass->ClassDefaultObject);
		AGameCueNotify_Actor* ActorCDO = Cast<AGameCueNotify_Actor>(Blueprint->GeneratedClass->ClassDefaultObject);
		
		if (StaticCDO || ActorCDO)
		{
			StringRefToRemove.SetPath(Blueprint->GeneratedClass->GetPathName());
		}
	}

	if (StringRefToRemove.IsValid())
	{
		TArray<FSoftObjectPath> StringRefs;
		StringRefs.Add(StringRefToRemove);
		
		
		for (UGameCueSet* Set : GetGlobalCueSets())
		{
			Set->RemoveCuesByStringRefs(StringRefs);
		}

		OnGameCueNotifyAddOrRemove.Broadcast();
	}
}

TArray<UGameCueSet*> UGameCueManager::GetGlobalCueSets()
{
	TArray<UGameCueSet*> Set;
	if (RuntimeGameCueObjectLibrary.CueSet)
	{
		Set.Add(RuntimeGameCueObjectLibrary.CueSet);
	}
	if (EditorGameCueObjectLibrary.CueSet)
	{
		Set.Add(EditorGameCueObjectLibrary.CueSet);
	}
	return Set;
}

UGameCueSet* UGameCueManager::GetRuntimeCueSet()
{
	return RuntimeGameCueObjectLibrary.CueSet;
}

void UGameCueManager::EndGameCuesFor(AActor* TargetActor)

{
	// Make a copy so that OnOwnerDestroyed can remove itself (if it so chooses)
	TArray<TObjectPtr<AActor>> Children = TargetActor->Children;
	for (AActor* Child : Children)
	{
		AGameCueNotify_Actor* NotifyActor = Cast<AGameCueNotify_Actor>(Child);
		if (NotifyActor)
		{
			NotifyActor->OnOwnerDestroyed(TargetActor);
		}
	}
}


void UGameCueManager::InitializeRuntimeObjectLibrary()
{
	UE_SCOPED_ENGINE_ACTIVITY(TEXT("Initializing GameCueManager Runtime Object Library"));

	RuntimeGameCueObjectLibrary.Paths = GetAlwaysLoadedGameCuePaths();
	if (RuntimeGameCueObjectLibrary.CueSet == nullptr)
	{
		RuntimeGameCueObjectLibrary.CueSet = NewObject<UGameCueSet>(this, TEXT("GlobalGameCueSet"));
	}

	RuntimeGameCueObjectLibrary.CueSet->Empty();
	RuntimeGameCueObjectLibrary.bHasBeenInitialized = true;
	
	RuntimeGameCueObjectLibrary.bShouldSyncScan = ShouldSyncScanRuntimeObjectLibraries();
	RuntimeGameCueObjectLibrary.bShouldSyncLoad = ShouldSyncLoadRuntimeObjectLibraries();
	RuntimeGameCueObjectLibrary.bShouldAsyncLoad = ShouldAsyncLoadRuntimeObjectLibraries();

	InitObjectLibrary(RuntimeGameCueObjectLibrary);
}

void UGameCueManager::HandleAssetRenamed(const FAssetData& Data, const FString& String)
{
	const FString ParentClassName = Data.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
	if (!ParentClassName.IsEmpty())
	{
		UClass* DataClass = FindObject<UClass>(nullptr, *ParentClassName);
		if (DataClass)
		{
			UGameCueNotify_Static* StaticCDO = Cast<UGameCueNotify_Static>(DataClass->ClassDefaultObject);
			AGameCueNotify_Actor* ActorCDO = Cast<AGameCueNotify_Actor>(DataClass->ClassDefaultObject);
			if (StaticCDO || ActorCDO)
			{
				VerifyNotifyAssetIsInValidPath(Data.PackagePath.ToString());

				for (UGameCueSet* Set : GetGlobalCueSets())
				{
					Set->UpdateCueByStringRefs(String + TEXT("_C"), Data.GetObjectPathString() + TEXT("_C"));
				}
				OnGameCueNotifyAddOrRemove.Broadcast();
			}
		}
	}
}


bool UGameCueManager::VerifyNotifyAssetIsInValidPath(FString Path)
{
	bool ValidPath = false;
	for (FString& str: GetValidGameCuePaths())
	{
		if (Path.Contains(str))
		{
			ValidPath = true;
		}
	}

	if (!ValidPath)
	{
		FString MessageTry = FString::Printf(TEXT("Warning: Invalid GameCue Path %s"), *Path);
		MessageTry += TEXT("\n\nGameCue Notifies should only be saved in the following folders:");

		UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Warning: Invalid GameCuePath: %s"), *Path);
				UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Valid Paths: "));
		for (FString& str: GetValidGameCuePaths())
		{
					UE_LOG(LogGMCAbilitySystem,Warning, TEXT("  %s"), *str);
			MessageTry += FString::Printf(TEXT("\n  %s"), *str);
		}

		MessageTry += FString::Printf(TEXT("\n\nThis asset must be moved to a valid location to work in game."));

		const FText MessageText = FText::FromString(MessageTry);
		const FText TitleText = NSLOCTEXT("GameCuePathWarning", "GameCuePathWarningTitle", "Invalid GameCue Path");
		FMessageDialog::Open(EAppMsgType::Ok, MessageText, TitleText);
	}

	return ValidPath;
}

void UGameCueManager::ReloadObjectLibrary(UWorld* World, const UWorld::InitializationValues IVS)
{
	if (bAccelerationMapOutdated)
	{
		RefreshObjectLibraries();
	}
}

void UGameCueManager::RefreshObjectLibraries()
{
	if (RuntimeGameCueObjectLibrary.bHasBeenInitialized)
	{
		check(RuntimeGameCueObjectLibrary.CueSet);
		RuntimeGameCueObjectLibrary.CueSet->Empty();
		InitObjectLibrary(RuntimeGameCueObjectLibrary);
	}

	if (EditorGameCueObjectLibrary.bHasBeenInitialized)
	{
		check(EditorGameCueObjectLibrary.CueSet);
		EditorGameCueObjectLibrary.CueSet->Empty();
		InitObjectLibrary(EditorGameCueObjectLibrary);
	}
}


UGameCueSet* UGameCueManager::GetEditorCueSet()
{

	return EditorGameCueObjectLibrary.CueSet;

}



UWorld* UGameCueManager::GetCachedWorldForGameCueNotifies()
{
#if WITH_EDITOR
	if (PreviewWorld)
		return PreviewWorld;
#endif

	return CurrentWorld;
}

int32 GameCueActorRecycle = 1;
static FAutoConsoleVariableRef CVarGameCueActorRecycle(TEXT("AbilitySystem.GameCueActorRecycle"), GameCueActorRecycle, TEXT("Allow recycling of GameCue Actors"), ECVF_Default );

int32 GameCueActorRecycleDebug = 0;
static FAutoConsoleVariableRef CVarGameCueActorRecycleDebug(TEXT("AbilitySystem.GameCueActorRecycleDebug"), GameCueActorRecycleDebug, TEXT("Prints logs for GC actor recycling debugging"), ECVF_Default );


AGameCueNotify_Actor* UGameCueManager::GetInstancedCueActor(AActor* TargetActor, UClass* GameCueNotifyActorClass,
	const FGameCueParameters& Parameters)
{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_GameCueManager_GetInstancedCueActor);

	const TSubclassOf<AGameCueNotify_Actor> CueClass = GameCueNotifyActorClass;
	if (!ensure(TargetActor) || !ensure(CueClass))
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("GetInstancedCueActor called with invalid parameters (TargetActor = %s, CueClass = %s)"), *GetNameSafe(TargetActor), *GetNameSafe(GameCueNotifyActorClass));
		return nullptr;
	}

	// There used to be special code here to handle the case where the TargetActor was a CDO.  I'm not sure why that would be (or how that's even possible -- perhaps in the default Blueprint Viewport? But I can't trigger it.)
	// Let's log it in case a user comes across this issue.
	//	Animtion preview hack. If we are trying to play the GC on a CDO, then don't use actor recycling and don't set the owner (to the CDO, which would cause problems)
	//	And we'll try to manually find (and reuse) the existing instance on the TargetActor.
	UE_CLOG(WITH_EDITOR && TargetActor->HasAnyFlags(RF_ClassDefaultObject), LogGMCAbilitySystem, Warning, TEXT("Adding %s to CDO %s. This used to be explicitly disallowed."), *GetNameSafe(CueClass), *GetNameSafe(TargetActor));

	UWorld* World = TargetActor->GetWorld();
	if (!World)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("GetInstancedCueActor called on TargetActor %s which did not belong to a world (is it a CDO or being destroyed?)"), *GetNameSafe(TargetActor));
		return nullptr;
	}
	UE_CLOG(CurrentWorld != World, LogGMCAbilitySystem, Error, TEXT("GetInstancedCueActor had CurrentWorld set to %s but TargetActor is in World %s"), *GetNameSafe(CurrentWorld), *GetNameSafe(World));

	// We found the exact Cue we're looking for already on the TargetActor.  Use that.
	AGameCueNotify_Actor* ExistingCueOnActor = FindExistingCueOnActor(*TargetActor, CueClass, Parameters);
	if (ExistingCueOnActor)
	{
		ExistingCueOnActor->CueInstigator = Parameters.GetInstigator();
		ExistingCueOnActor->CueSourceObject = Parameters.GetSourceObject();
		return ExistingCueOnActor;
	}

	const bool bUseActorRecycling = (GameCueActorRecycle > 0);
	if (bUseActorRecycling)
	{
		if (AGameCueNotify_Actor* RecycledCue = FindRecycledCue(CueClass, *World))
		{
			RecycledCue->bInRecycleQueue = false;
			RecycledCue->SetOwner(TargetActor);
			RecycledCue->SetActorLocationAndRotation(TargetActor->GetActorLocation(), TargetActor->GetActorRotation());
			RecycledCue->ReuseAfterRecycle();
			RecycledCue->CueInstigator = Parameters.GetInstigator();
			RecycledCue->CueSourceObject = Parameters.GetSourceObject();

			//UE_CLOG((GameCueActorRecycleDebug > 0), LogGMCAbilitySystem, Display, TEXT("GetInstancedCueActor reusing Recycled CueActor: %s"), *GetNameSafe(RecycledCue));

			
/* Should we keep the sequence recorder
#if WITH_EDITOR
			// let things know that we 'spawned'
			ISequenceRecorder& SequenceRecorder = FModuleManager::LoadModuleChecked<ISequenceRecorder>("SequenceRecorder");
			SequenceRecorder.NotifyActorStartRecording(RecycledCue);
#endif
*/
			return RecycledCue;
		}
	}

	// If we can't reuse, then spawn a new one. Since TargetActor is the Owner, a reference to this CueNotify Actor will live in TargetActor::Children.
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = TargetActor;
	SpawnParams.OverrideLevel = World->PersistentLevel;
	AGameCueNotify_Actor* SpawnedCue = World->SpawnActor<AGameCueNotify_Actor>(CueClass, TargetActor->GetActorLocation(), TargetActor->GetActorRotation(), SpawnParams);
	SpawnedCue->CueInstigator = Parameters.GetInstigator();
	SpawnedCue->CueSourceObject = Parameters.GetSourceObject();

	//UE_CLOG(LogGameCueActorSpawning > 0, LogGMCAbilitySystem, Warning, TEXT("Spawned Gameplay Cue Notify Actor: %s (instance %s)"), *CueClass->GetName(), *GetNameSafe(SpawnedCue));

	return SpawnedCue;
}


AGameCueNotify_Actor* UGameCueManager::FindRecycledCue(const TSubclassOf<AGameCueNotify_Actor>& CueClass, const UWorld& FindInWorld)
{
	FPreallocationInfo& Info = GetPreallocationInfo(&FindInWorld);
	FGameCueNotifyActorArray* PreallocatedList = Info.PreallocatedInstances.Find(CueClass);
	if (!PreallocatedList)
	{
		// No preallocated instances yet
		return nullptr;
	}

	while (PreallocatedList->Actors.Num() > 0)
	{
		AGameCueNotify_Actor* RecycledCue = PreallocatedList->Actors.Pop(EAllowShrinking::No);

		// Normal check: if cue was destroyed or is pending kill, then don't use it.
		if (IsValid(RecycledCue))
		{
			return RecycledCue;
		}
					
		// outside of replays, this should not happen. GC Notifies should not be actually destroyed.
		ensureMsgf(FindInWorld.IsPlayingReplay(), TEXT("RecycledCue is pending kill, garbage or null: %s."), *GetNameSafe(RecycledCue));
	}

	return nullptr;
}
FPreallocationInfo& UGameCueManager::GetPreallocationInfo(const UWorld* World)
{
	FObjectKey ObjKey(World);

	for (FPreallocationInfo& Info : PreallocationInfoList_Internal)
	{
		if (ObjKey == Info.OwningWorldKey)
		{
			return Info;
		}
	}

	FPreallocationInfo NewInfo;
	NewInfo.OwningWorldKey = ObjKey;

	PreallocationInfoList_Internal.Add(NewInfo);
	return PreallocationInfoList_Internal.Last();
}

AGameCueNotify_Actor* UGameCueManager::FindExistingCueOnActor(const AActor& TargetActor, const TSubclassOf<AGameCueNotify_Actor>& CueClass, const FGameCueParameters& Parameters) const
{
	for (AActor* Child : TargetActor.Children)
	{
		if (IsValid(Child) && Child->IsA(CueClass))
		{
			AGameCueNotify_Actor* ChildNotify = CastChecked<AGameCueNotify_Actor>(Child);

			// Somehow the LifeSpan can end up being zero, meaning we're about to be destroyed (so don't reuse)
			if (ChildNotify->GameCuePendingRemove())
			{
				UE_LOG(LogGMCAbilitySystem, Verbose, TEXT("FindExistingCueActor considered %s, but it was pending remove"), *GetNameSafe(ChildNotify));
				continue;
			}

			const bool bInstigatorMatches = !ChildNotify->bUniqueInstancePerInstigator || ChildNotify->CueInstigator == Parameters.GetInstigator();
			const bool bSourceMatches = !ChildNotify->bUniqueInstancePerSourceObject || ChildNotify->CueSourceObject == Parameters.GetSourceObject();
			if (bInstigatorMatches && bSourceMatches)
			{
				return ChildNotify;
			}
		}
	}

	return nullptr;
}

void UGameCueManager::NotifyGameCueActorFinished(AGameCueNotify_Actor* Actor)
{
}

void UGameCueManager::NotifyGameCueActorEndPlay(AGameCueNotify_Actor* Actor)
{
}

void UGameCueManager::HandleGameCues(AActor* TargetActor, const FGameplayTagContainer& GameCueTags,
	EGameCueEvent::Type EventType, const FGameCueParameters& Parameters, EGameCueExecutionOptions Options)
{
#if WITH_EDITOR
	if (GIsEditor && TargetActor == nullptr && UGameCueManager::PreviewComponent)
	{
		TargetActor = GetMutableDefault<AActor>();
	}
#endif

	if (!(Options & EGameCueExecutionOptions::IgnoreSuppression) && ShouldSuppressGameCues(TargetActor))
	{
		return;
	}

	for (auto It = GameCueTags.CreateConstIterator(); It; ++It)
	{
		HandleGameCue(TargetActor, *It, EventType, Parameters, Options);
	}
}

void UGameCueManager::HandleGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType,
	const FGameCueParameters& Parameters, EGameCueExecutionOptions Options)
{
	
#if WITH_SERVER_CODE
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GameCueManager_HandleGameCue);
#endif

#if WITH_EDITOR
	if (GIsEditor && TargetActor == nullptr && UGameCueManager::PreviewComponent)
	{
		TargetActor = Cast<AActor>(AActor::StaticClass()->GetDefaultObject());
	}
#endif

	if (!(Options & EGameCueExecutionOptions::IgnoreSuppression) && ShouldSuppressGameCues(TargetActor))
	{
		return;
	}

	if (!(Options & EGameCueExecutionOptions::IgnoreTranslation))
	{
		TranslateGameCue(GameCueTag, TargetActor, Parameters);
	}
	
	RouteGameCue(TargetActor, GameCueTag, EventType, Parameters, Options);
}

bool UGameCueManager::ShouldSuppressGameCues(AActor* TargetActor)
{
	if (DisableGameCues ||
		!TargetActor ||
		(GameCueRunOnDedicatedServer == 0 && IsDedicatedServerForGameCue()))
	{
		return true;
	}

	return false;
}

void UGameCueManager::TranslateGameCue(FGameplayTag& Tag, AActor* TargetActor, const FGameCueParameters& Parameters)
{
}

void UGameCueManager::RouteGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType,
	const FGameCueParameters& Parameters, EGameCueExecutionOptions Options)
{
	
#if WITH_SERVER_CODE
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GameCueManager_RouteGameCue);
#endif

	// If we want to ignore interfaces, set the pointer to null
	IGameCueInterface* GameCueInterface = !(Options & EGameCueExecutionOptions::IgnoreInterfaces) ? Cast<IGameCueInterface>(TargetActor) : nullptr;
	bool bAcceptsCue = true;
	if (GameCueInterface)
	{
		bAcceptsCue = GameCueInterface->ShouldAcceptGameCue(TargetActor, GameCueTag, EventType, Parameters);
	}

#if !UE_BUILD_SHIPPING
	if (OnRouteGameCue.IsBound())
	{
		OnRouteGameCue.Broadcast(TargetActor, GameCueTag, EventType, Parameters, Options);
	}
#endif // !UE_BUILD_SHIPPING

#if ENABLE_DRAW_DEBUG
	if (DisplayGameCues && !(Options & EGameCueExecutionOptions::IgnoreDebug))
	{
		FString DebugStr = FString::Printf(TEXT("[%s] %s"), *GetNameSafe(TargetActor), *GameCueTag.ToString() );
		FColor DebugColor = FColor::Green;
		DrawDebugString(TargetActor->GetWorld(), FVector(0.f, 0.f, 100.f), DebugStr, TargetActor, DebugColor, DisplayGameCueDuration);
		UE_LOG(LogGMCAbilitySystem,Display, TEXT("%s"), *DebugStr);
	}
#endif // ENABLE_DRAW_DEBUG

	CurrentWorld = TargetActor->GetWorld();

	// Don't handle gameplay cues when world is tearing down
	if (!CurrentWorld/*|| GetWorld()->bIsTearingDown*/)
	{
		UE_LOG(LogGMCAbilitySystem,Error, TEXT("No world"));

		return;
	}

	// Give the global set a chance
	if (bAcceptsCue && !(Options & EGameCueExecutionOptions::IgnoreNotifies))
	{
		RuntimeGameCueObjectLibrary.CueSet->HandleGameCue(TargetActor, GameCueTag, EventType, Parameters);
	}

	// Use the interface even if it's not in the map
	if (GameCueInterface && bAcceptsCue)
	{
		GameCueInterface->HandleGameCue(TargetActor, GameCueTag, EventType, Parameters);
	}

	// This is to force client side replays to record the target of the GC on the next frame. (ForceNetUpdates from the server will not translate into ForceNetUpdates on the client/replays)
	TargetActor->ForceNetUpdate();

	CurrentWorld = nullptr;
}

TSharedPtr<FStreamableHandle> UGameCueManager::InitObjectLibrary(FGameCueObjectLibrary& Library)
{
	TSharedPtr<FStreamableHandle> RetVal;

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Loading Library"), STAT_ObjectLibrary, STATGROUP_LoadTime);

	// Instantiate the UObjectLibraries if they aren't there already
	if (!Library.StaticObjectLibrary)
	{
		Library.StaticObjectLibrary = UObjectLibrary::CreateLibrary(UGameCueNotify_Static::StaticClass(), true, GIsEditor && !IsRunningCommandlet());
		if (GIsEditor)
		{
			Library.StaticObjectLibrary->bIncludeOnlyOnDiskAssets = false;
		}
	}
	
	if (!Library.ActorObjectLibrary)
	{
		Library.ActorObjectLibrary = UObjectLibrary::CreateLibrary(AGameCueNotify_Actor::StaticClass(), true, GIsEditor && !IsRunningCommandlet());
		if (GIsEditor)
		{
			Library.ActorObjectLibrary->bIncludeOnlyOnDiskAssets = false;
		}
	}	

	Library.bHasBeenInitialized = true;

#if WITH_EDITOR
	bAccelerationMapOutdated = false;
#endif

	FScopeCycleCounterUObject PreloadScopeActor(Library.ActorObjectLibrary);

	// ------------------------------------------------------------------------------------------------------------------
	//	Scan asset data. If bShouldSyncScan is false, whatever state the asset registry is in will be what is returned.
	// ------------------------------------------------------------------------------------------------------------------

	{
		SCOPE_LOG_TIME_IN_SECONDS(*FString::Printf(TEXT("UGameCueManager::InitObjectLibraries    Actors. Paths: %s"), *FString::Join(Library.Paths, TEXT(", "))), nullptr)
		Library.ActorObjectLibrary->LoadBlueprintAssetDataFromPaths(Library.Paths, Library.bShouldSyncScan);
	}
	{
		SCOPE_LOG_TIME_IN_SECONDS(*FString::Printf(TEXT("UGameCueManager::InitObjectLibraries    Objects")), nullptr)
		Library.StaticObjectLibrary->LoadBlueprintAssetDataFromPaths(Library.Paths, Library.bShouldSyncScan);
	}

	// ---------------------------------------------------------
	// Sync load if told to do so	
	// ---------------------------------------------------------
	if (Library.bShouldSyncLoad)
	{
#if STATS
		FString PerfMessage = FString::Printf(TEXT("Fully Loaded GameCueNotify object library"));
		SCOPE_LOG_TIME_IN_SECONDS(*PerfMessage, nullptr)
#endif
		Library.ActorObjectLibrary->LoadAssetsFromAssetData();
		Library.StaticObjectLibrary->LoadAssetsFromAssetData();
	}

	// ---------------------------------------------------------
	// Look for GameCueNotifies that handle events
	// ---------------------------------------------------------
	
	TArray<FAssetData> ActorAssetDatas;
	Library.ActorObjectLibrary->GetAssetDataList(ActorAssetDatas);

	TArray<FAssetData> StaticAssetDatas;
	Library.StaticObjectLibrary->GetAssetDataList(StaticAssetDatas);

	TArray<FGameCueReferencePair> CuesToAdd;
	TArray<FSoftObjectPath> AssetsToLoad;

	// ------------------------------------------------------------------------------------------------------------------
	// Build Cue lists for loading. Determines what from the obj library needs to be loaded
	// ------------------------------------------------------------------------------------------------------------------
	BuildCuesToAddToGlobalSet(ActorAssetDatas, GET_MEMBER_NAME_CHECKED(AGameCueNotify_Actor, GameCueName), CuesToAdd, AssetsToLoad, Library.ShouldLoad);
	BuildCuesToAddToGlobalSet(StaticAssetDatas, GET_MEMBER_NAME_CHECKED(UGameCueNotify_Static, GameCueName), CuesToAdd, AssetsToLoad, Library.ShouldLoad);

	const FName PropertyName = GET_MEMBER_NAME_CHECKED(AGameCueNotify_Actor, GameCueName);
	check(PropertyName == GET_MEMBER_NAME_CHECKED(UGameCueNotify_Static, GameCueName));

	// ------------------------------------------------------------------------------------------------------------------------------------
	// Add these cues to the set. The UGameCueSet is the data structure used in routing the gameplay cue events at runtime.
	// ------------------------------------------------------------------------------------------------------------------------------------
	UGameCueSet* SetToAddTo = Library.CueSet;
	if (!SetToAddTo)
	{
		SetToAddTo = RuntimeGameCueObjectLibrary.CueSet;
	}
	check(SetToAddTo);
	SetToAddTo->AddCues(CuesToAdd);

	// --------------------------------------------
	// Start loading them if necessary
	// --------------------------------------------
	if (Library.bShouldAsyncLoad)
	{
		auto ForwardLambda = [](TArray<FSoftObjectPath> AssetList, FOnGameCueNotifySetLoaded OnLoadedDelegate)
		{
			OnLoadedDelegate.ExecuteIfBound(AssetList);
		};

		if (AssetsToLoad.Num() > 0)
		{
			FStreamableDelegate Del = FStreamableDelegate::CreateStatic(ForwardLambda, AssetsToLoad, Library.OnLoaded);
			GameCueAssetHandle = StreamableManager.RequestAsyncLoad(MoveTemp(AssetsToLoad), MoveTemp(Del), Library.AsyncPriority);
			RetVal = GameCueAssetHandle;
		}
		else
		{
			// Still fire the delegate even if nothing was found to load
			Library.OnLoaded.ExecuteIfBound(MoveTemp(AssetsToLoad));
		}
	}

	// Build Tag Translation table
	//TranslationManager.BuildTagTranslationTable();
	return RetVal;
}

static FAutoConsoleVariable CVarGameCueAddToGlobalSetDebug(TEXT("GameCue.AddToGlobalSet.DebugTag"), TEXT(""), TEXT("Debug Tag adding to global set"), ECVF_Default	);


void UGameCueManager::BuildCuesToAddToGlobalSet(const TArray<FAssetData>& AssetDataList, FName TagPropertyName, TArray<FGameCueReferencePair>& OutCuesToAdd, TArray<FSoftObjectPath>& OutAssetsToLoad, FShouldLoadGCNotifyDelegate ShouldLoad)
{
	UGameplayTagsManager& Manager = UGameplayTagsManager::Get();

	OutAssetsToLoad.Reserve(OutAssetsToLoad.Num() + AssetDataList.Num());

	for (const FAssetData& Data: AssetDataList)
	{
		const FName FoundGameplayTag = Data.GetTagValueRef<FName>(TagPropertyName);
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (CVarGameCueAddToGlobalSetDebug->GetString().IsEmpty() == false && FoundGameplayTag.ToString().Contains(CVarGameCueAddToGlobalSetDebug->GetString()))
		{
			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Adding Tag %s to GlobalSet"), *FoundGameplayTag.ToString());
		}
#endif

		// If ShouldLoad delegate is bound and it returns false, don't load this one
		if (ShouldLoad.IsBound() && (ShouldLoad.Execute(Data, FoundGameplayTag) == false))
		{
			continue;
		}
		
		if (ShouldLoadGameCueAssetData(Data) == false)
		{
			continue;
		}
		
		if (!FoundGameplayTag.IsNone())
		{
			const FString GeneratedClassTag = Data.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
			if (GeneratedClassTag.IsEmpty())
			{
				UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Unable to find GeneratedClass value for AssetData %s"), *Data.GetObjectPathString());
				continue;
			}

			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("GameCueManager Found: %s / %s"), *FoundGameplayTag.ToString(), *GeneratedClassTag);

			FGameplayTag  GameCueTag = Manager.RequestGameplayTag(FoundGameplayTag, false);
			if (GameCueTag.IsValid())
			{
				// Add a new NotifyData entry to our flat list for this one
				FSoftObjectPath StringRef;
				StringRef.SetPath(FPackageName::ExportTextPathToObjectPath(GeneratedClassTag));

				OutCuesToAdd.Add(FGameCueReferencePair(GameCueTag, StringRef));

				OutAssetsToLoad.Add(StringRef);

				// Make sure core knows about this ref so it can be properly detected during cook.
				StringRef.PostLoadPath(GetLinker());
			}
			else
			{
				// Warn about this tag but only once to cut down on spam (we may build cue sets multiple times in the editor)
				static TSet<FName> WarnedTags;
				if (WarnedTags.Contains(FoundGameplayTag) == false)
				{
					UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Found GameCue tag %s in asset %s but there is no corresponding tag in the GameplayTagManager."), *FoundGameplayTag.ToString(), *Data.PackageName.ToString());
					WarnedTags.Add(FoundGameplayTag);
				}
			}
		}
	}
}


TArray<FString> UGameCueManager::GetAlwaysLoadedGameCuePaths()
{
	return UGMCAbilitySystemGlobals::Get().GetGameCueNotifyPaths();
}

bool UGameCueManager::IsGameCueRecylingEnabled()
{
	return GameCueActorRecycle > 0;
}




bool UGameCueManager::ShouldSyncScanRuntimeObjectLibraries() const
{
	return true;
}

bool UGameCueManager::ShouldSyncLoadRuntimeObjectLibraries() const
{
	return false;
}

bool UGameCueManager::ShouldAsyncLoadRuntimeObjectLibraries() const
{
	return true;
}

#endif // WITH_EDITOR
