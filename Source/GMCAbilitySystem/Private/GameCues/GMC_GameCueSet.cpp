// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCues/GMC_GameCueSet.h"
#include "GameplayTagsManager.h"
#include "GameplayTagsModule.h"
//#include "AbilitySystemGlobals.h"
//#include "AbilitySystemLog.h"
//#include "GameCueNotify_Actor.h"
#include "GameCues/GameCueNotify_Static.h"
#include "GMCAbilitySystemGlobals.h"
#include "GameCues/GMC_GameCueManager.h"
#include "NativeGameplayTags.h"

//#include UE_INLINE_GENERATED_CPP_BY_NAME(GameCueSet)

UE_DEFINE_GAMEPLAY_TAG_STATIC(StaticTag_GameCue, TEXT("GameCue"));

namespace GameCueDebug
{
	static FGameplayTagContainer DebugGameCueFilter;
	FAutoConsoleCommand ConCommandDebugGameCueFilter(
		TEXT("GameCue.FilterCuesByTag"),
		TEXT("Adds or removes a GameCue tag to the debug filter list. If the filter is populated, only GameCues with tags in the filter will be invoked."),
		FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args) 
			{
				for (const FString& TagToFilter : Args)
				{
					const FGameplayTag ExistingTag = FGameplayTag::RequestGameplayTag(FName(TagToFilter));
					if (!ExistingTag.IsValid())
					{
						continue;
					}

					if (DebugGameCueFilter.HasTagExact(ExistingTag))
					{
						DebugGameCueFilter.RemoveTag(ExistingTag);
					}
					else
					{
						DebugGameCueFilter.AddTagFast(ExistingTag);
					}
				}
			})
	);
}

// Let's use this to create a path for testing what happens when GameCues are failing to load (or are loading slowly due to IO contention)
static TAutoConsoleVariable<bool> CVarGameCueFailLoads(TEXT("AbilitySystem.GameCueFailLoads"), false, TEXT("Pretend all GameCues are unloaded (and keep requesting loads) while true. Set to false to allow GameCues to play."), ECVF_Default);

// --------------------------------------------------------------------------------------------------------------------------------------------------------
//
//	UGameCueSet
//
// --------------------------------------------------------------------------------------------------------------------------------------------------------


UGameCueSet::UGameCueSet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{

}

bool UGameCueSet::HandleGameCue(AActor* TargetActor, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
#if WITH_SERVER_CODE
	QUICK_SCOPE_CYCLE_COUNTER(STAT_GameCueSet_HandleGameCue);
#endif
	
#if !UE_BUILD_SHIPPING
	if (!GameCueDebug::DebugGameCueFilter.IsEmpty())
	{
		if (!GameCueDebug::DebugGameCueFilter.HasTagExact(GameCueTag))
		{
			return false;
		}
	}
#endif

	// GameCueTags could have been removed from the dictionary but not content. When the content is resaved the old tag will be cleaned up, but it could still come through here
	// at runtime. Since we only populate the map with dictionary GameCue tags, we may not find it here.
	int32* Ptr = GameCueDataMap.Find(GameCueTag);
	if (Ptr && *Ptr != INDEX_NONE)
	{
		int32 DataIdx = *Ptr;

		// TODO - resolve internal handler modifying params before passing them on with new const-ref params.
		FGameCueParameters writableParameters = Parameters;
		return HandleGameCueNotify_Internal(TargetActor, DataIdx, EventType, writableParameters);
	}

	return false;
}

void UGameCueSet::AddCues(const TArray<FGameCueReferencePair>& CuesToAdd)
{
	if (CuesToAdd.Num() > 0)
	{
		for (const FGameCueReferencePair& CueRefPair : CuesToAdd)
		{
			const FGameplayTag& GameCueTag = CueRefPair.GameCueTag;
			const FSoftObjectPath& StringRef = CueRefPair.StringRef;

			// Check for duplicates: we may want to remove this eventually (allow multiple GC notifies to handle same event)
			bool bDupe = false;
			for (FGameCueNotifyData& Data : GameCueData)
			{
				if (Data.GameCueTag == GameCueTag)
				{
					if (StringRef.ToString() != Data.GameCueNotifyObj.ToString())
					{
						///ABILITY_LOG(Warning, TEXT("AddGameCueData_Internal called for [%s,%s] when it already existed [%s,%s]. Skipping."), *GameCueTag.ToString(), *StringRef.ToString(), *Data.GameCueTag.ToString(), *Data.GameCueNotifyObj.ToString());
					}
					bDupe = true;
					break;
				}
			}

			if (bDupe)
			{
				continue;
			}

			FGameCueNotifyData NewData;
			NewData.GameCueNotifyObj = StringRef;
			NewData.GameCueTag = GameCueTag;

			GameCueData.Add(NewData);
		}

		BuildAccelerationMap_Internal();
	}
}

void UGameCueSet::RemoveCuesByTags(const FGameplayTagContainer& TagsToRemove)
{
}

void UGameCueSet::RemoveCuesByStringRefs(const TArray<FSoftObjectPath>& CuesToRemove)
{
	for (const FSoftObjectPath& StringRefToRemove : CuesToRemove)
	{
		for (int32 idx = 0; idx < GameCueData.Num(); ++idx)
		{
			if (GameCueData[idx].GameCueNotifyObj == StringRefToRemove)
			{
				GameCueData.RemoveAt(idx);
				BuildAccelerationMap_Internal();
				break;
			}
		}
	}
}

void UGameCueSet::RemoveLoadedClass(UClass* Class)
{
	for (int32 idx = 0; idx < GameCueData.Num(); ++idx)
	{
		if (GameCueData[idx].LoadedGameCueClass == Class)
		{
			GameCueData[idx].LoadedGameCueClass = nullptr;
		}
	}
}

void UGameCueSet::GetFilenames(TArray<FString>& Filenames) const
{
	Filenames.Reserve(GameCueData.Num());
	for (const FGameCueNotifyData& Data : GameCueData)
	{
		Filenames.Add(Data.GameCueNotifyObj.GetLongPackageName());
	}
}

void UGameCueSet::GetSoftObjectPaths(TArray<FSoftObjectPath>& List) const
{
	List.Reserve(GameCueData.Num());
	for (const FGameCueNotifyData& Data : GameCueData)
	{
		List.Add(Data.GameCueNotifyObj);
	}
}

#if WITH_EDITOR
void UGameCueSet::UpdateCueByStringRefs(const FSoftObjectPath& CueToRemove, FString NewPath)
{
	for (int32 idx = 0; idx < GameCueData.Num(); ++idx)
	{
		if (GameCueData[idx].GameCueNotifyObj == CueToRemove)
		{
			GameCueData[idx].GameCueNotifyObj = NewPath;
			BuildAccelerationMap_Internal();
			break;
		}
	}
}

void UGameCueSet::CopyCueDataToSetForEditorPreview(FGameplayTag Tag, UGameCueSet* DestinationSet)
{
	const int32 SourceIdx = GameCueData.IndexOfByPredicate([&Tag](const FGameCueNotifyData& Data) { return Data.GameCueTag == Tag; });
	if (SourceIdx == INDEX_NONE)
	{
		// Doesn't exist in source, so nothing to copy
		return;
	}

	int32 DestIdx = DestinationSet->GameCueData.IndexOfByPredicate([&Tag](const FGameCueNotifyData& Data) { return Data.GameCueTag == Tag; });
	if (DestIdx == INDEX_NONE)
	{
		// wholesale copy
		DestIdx = DestinationSet->GameCueData.Num();
		DestinationSet->GameCueData.Add(GameCueData[SourceIdx]);

		DestinationSet->BuildAccelerationMap_Internal();
	}
	else
	{
		// Update only if we need to
		if (DestinationSet->GameCueData[DestIdx].GameCueNotifyObj.IsValid() == false)
		{
			DestinationSet->GameCueData[DestIdx].GameCueNotifyObj = GameCueData[SourceIdx].GameCueNotifyObj;
			DestinationSet->GameCueData[DestIdx].LoadedGameCueClass = GameCueData[SourceIdx].LoadedGameCueClass;
		}

	}

	// Start async load
	UGameCueManager* CueManager = UGMCAbilitySystemGlobals::Get().GetGameCueManager();
	if (ensure(CueManager))
	{
		CueManager->StreamableManager.RequestAsyncLoad(DestinationSet->GameCueData[DestIdx].GameCueNotifyObj);
	}
}

#endif

void UGameCueSet::Empty()
{
	GameCueData.Empty();
	GameCueDataMap.Empty();
}

void UGameCueSet::PrintCues() const
{
	FGameplayTagContainer AllGameCueTags = UGameplayTagsManager::Get().RequestGameplayTagChildren(BaseGameCueTag());

	for (FGameplayTag ThisGameCueTag : AllGameCueTags)
	{
		int32 idx = GameCueDataMap.FindChecked(ThisGameCueTag);
		if (idx != INDEX_NONE)
		{
			//ABILITY_LOG(Warning, TEXT("   %s -> %d"), *ThisGameCueTag.ToString(), idx);
		}
		else
		{
			//ABILITY_LOG(Warning, TEXT("   %s -> unmapped"), *ThisGameCueTag.ToString());
		}
	}
}

bool UGameCueSet::HandleGameCueNotify_Internal(AActor* TargetActor, int32 DataIdx, EGameCueEvent::Type EventType, FGameCueParameters& Parameters)
{	
	bool bReturnVal = false;

	UGameCueManager* CueManager = UGMCAbilitySystemGlobals::Get().GetGameCueManager();
	if (!ensure(CueManager))
	{
		return false;
	}

	if (DataIdx != INDEX_NONE)
	{
		check(GameCueData.IsValidIndex(DataIdx));

		FGameCueNotifyData& CueData = GameCueData[DataIdx];

		Parameters.MatchedTagName = CueData.GameCueTag;

		const bool bDebugFailLoads = CVarGameCueFailLoads.GetValueOnGameThread();

		// If object is not loaded yet
		if (CueData.LoadedGameCueClass == nullptr || bDebugFailLoads)
		{
			// See if the object is loaded but just not hooked up here
			CueData.LoadedGameCueClass = Cast<UClass>(CueData.GameCueNotifyObj.ResolveObject());
			if (CueData.LoadedGameCueClass == nullptr || bDebugFailLoads)
			{
				if (!CueManager->HandleMissingGameCue(this, CueData, TargetActor, EventType, Parameters))
				{
					return false;
				}
			}
		}

		check(CueData.LoadedGameCueClass);

		// Handle the Notify if we found something
		if (UGameCueNotify_Static* NonInstancedCue = Cast<UGameCueNotify_Static>(CueData.LoadedGameCueClass->ClassDefaultObject))
		{
			if (NonInstancedCue->HandlesEvent(EventType))
			{
				NonInstancedCue->HandleGameCue(TargetActor, EventType, Parameters);
				bReturnVal = true;
				if (!NonInstancedCue->IsOverride)
				{
					HandleGameCueNotify_Internal(TargetActor, CueData.ParentDataIdx, EventType, Parameters);
				}
			}
			else
			{
				//Didn't even handle it, so IsOverride should not apply.
				HandleGameCueNotify_Internal(TargetActor, CueData.ParentDataIdx, EventType, Parameters);
			}
		}
		/*
		else if (AGameCueNotify_Actor* InstancedCue = Cast<AGameCueNotify_Actor>(CueData.LoadedGameCueClass->ClassDefaultObject))
		{
			bool bShouldDestroy = false;
			if (EventType == EGameCueEvent::Executed && !Parameters.bAbilityEffectActive && InstancedCue->bAutoDestroyOnRemove)
			{
				bShouldDestroy = true;
			}

			if (InstancedCue->HandlesEvent(EventType))
			{
				if (TargetActor)
				{
					TSubclassOf<AGameCueNotify_Actor> InstancedClass = InstancedCue->GetClass();

					//Get our instance. We should probably have a flag or something to determine if we want to reuse or stack instances. That would mean changing our map to have a list of active instances.
					AGameCueNotify_Actor* SpawnedInstancedCue = CueManager->GetInstancedCueActor(TargetActor, InstancedClass, Parameters);
					if (ensure(SpawnedInstancedCue))
					{
						SpawnedInstancedCue->HandleGameCue(TargetActor, EventType, Parameters);
						bReturnVal = true;
						if (!SpawnedInstancedCue->IsOverride)
						{
							HandleGameCueNotify_Internal(TargetActor, CueData.ParentDataIdx, EventType, Parameters);
						}

						if (bShouldDestroy)
						{
							SpawnedInstancedCue->HandleGameCue(TargetActor, EGameCueEvent::Removed, Parameters);
						}
					}
				}
			}*/
			else
			{
				//Didn't even handle it, so IsOverride should not apply.
				HandleGameCueNotify_Internal(TargetActor, CueData.ParentDataIdx, EventType, Parameters);
			}
		}
	/*}*/

	return bReturnVal;
}

void UGameCueSet::BuildAccelerationMap_Internal()
{
	// ---------------------------------------------------------
	//	Build up the rest of the acceleration map: every GameCue tag should have an entry in the map that points to the index into GameCueData to use when it is invoked.
	//	(or to -1 if no GameCueNotify is associated with that tag)
	// 
	// ---------------------------------------------------------

	GameCueDataMap.Empty();
	GameCueDataMap.Add(BaseGameCueTag()) = INDEX_NONE;

	for (int32 idx = 0; idx < GameCueData.Num(); ++idx)
	{
		GameCueDataMap.FindOrAdd(GameCueData[idx].GameCueTag) = idx;
	}

	FGameplayTagContainer AllGameCueTags = UGameplayTagsManager::Get().RequestGameplayTagChildren(BaseGameCueTag());


	// Create entries for children.
	// E.g., if "a.b" notify exists but "a.b.c" does not, point "a.b.c" entry to "a.b"'s notify.
	for (FGameplayTag ThisGameCueTag : AllGameCueTags)
	{
		if (GameCueDataMap.Contains(ThisGameCueTag))
		{
			continue;
		}

		FGameplayTag Parent = ThisGameCueTag.RequestDirectParent();

		int32 ParentValue = GameCueDataMap.FindChecked(Parent);
		GameCueDataMap.Add(ThisGameCueTag, ParentValue);
	}


	// Build up parentIdx on each item in GameCueData
	for (FGameCueNotifyData& Data : GameCueData)
	{
		FGameplayTag Parent = Data.GameCueTag.RequestDirectParent();
		while (Parent != BaseGameCueTag() && Parent.IsValid())
		{
			int32* idxPtr = GameCueDataMap.Find(Parent);
			if (idxPtr)
			{
				Data.ParentDataIdx = *idxPtr;
				break;
			}
			Parent = Parent.RequestDirectParent();
			if (Parent.GetTagName() == NAME_None)
			{
				break;
			}
		}
	}

	// PrintGameCueNotifyMap();
}

FGameplayTag UGameCueSet::BaseGameCueTag()
{
	// Note we should not cache this off as a static variable, since for new projects the GameCue tag will not be found until one is created.
	return StaticTag_GameCue.GetTag();
}

