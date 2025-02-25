// Copyright Epic Games, Inc. All Rights Reserved.

#include "..\Public\GMCAbilitySystemGlobals.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Pawn.h"
#include "Components/GMCAbilityComponent.h"

#include "HAL/LowLevelMemTracker.h"
#include "GameFramework/PlayerController.h"
#include "GameCues/GMC_GameCueManager.h"

#include "GameplayTagsManager.h"
#include "Engine/Engine.h"
#include "UObject/UObjectIterator.h"

UGMCAbilitySystemGlobals* UGMCAbilitySystemGlobals::SingletonInstance = nullptr;


UGMCAbilitySystemGlobals::UGMCAbilitySystemGlobals(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AbilitySystemGlobalsClassName = FSoftClassPath(TEXT("/Script/GMCAbilitySystem.GMCAbilitySystemGlobals"));
	bUseDebugTargetFromHud = false;
	MinimalReplicationTagCountBits = 5;
}

void UGMCAbilitySystemGlobals::InitGlobalData()
{
}

bool UGMCAbilitySystemGlobals::IsAbilitySystemGlobalsInitialized() const
{
	return true;
}

void UGMCAbilitySystemGlobals::StartAsyncLoadingObjectLibraries()
{
	if (GlobalGameCueManager != nullptr)
	{
		GlobalGameCueManager->InitializeRuntimeObjectLibrary();
	}
}

UGMC_AbilitySystemComponent* UGMCAbilitySystemGlobals::GetAbilitySystemComponentFromActor(const AActor* Actor,
                                                                                          bool LookForComponent)
{
	return  nullptr;

}

FGameplayAbilityActorInfo* UGMCAbilitySystemGlobals::AllocAbilityActorInfo() const
{
	return  nullptr;

}

FAbilityEffectContext* UGMCAbilitySystemGlobals::AllocAbilityEffectContext() const
{

	return  nullptr;
}

bool UGMCAbilitySystemGlobals::DeriveGameCueTagFromAssetName(FString AssetName, FGameplayTag& GameCueTag,
	FName& GameCueName)
{
	FGameplayTag OriginalTag = GameCueTag;
	
	// In the editor, attempt to infer GameCueTag from our asset name (if there is no valid GameCueTag already).
#if WITH_EDITOR
	if (GIsEditor)
	{
		if (GameCueTag.IsValid() == false)
		{
			AssetName.RemoveFromStart(TEXT("Default__"));
			AssetName.RemoveFromStart(TEXT("REINST_"));
			AssetName.RemoveFromStart(TEXT("SKEL_"));
			AssetName.RemoveFromStart(TEXT("GC_"));		// allow GC_ prefix in asset name
			AssetName.RemoveFromEnd(TEXT("_c"));

			AssetName.ReplaceInline(TEXT("_"), TEXT("."), ESearchCase::CaseSensitive);

			if (!AssetName.Contains(TEXT("GameCue")))
			{
				AssetName = FString(TEXT("GameCue.")) + AssetName;
			}

			GameCueTag = UGameplayTagsManager::Get().RequestGameplayTag(FName(*AssetName), false);
		}
		GameCueName = GameCueTag.GetTagName();
	}
#endif
	return (OriginalTag != GameCueTag);
}

UGameCueManager* UGMCAbilitySystemGlobals::GetGameCueManager()
{
	if (GlobalGameCueManager == nullptr)
	{
		// Load specific GameCue manager object if specified
		if (GlobalGameCueManagerName.IsValid())
		{
			GlobalGameCueManager = LoadObject<UGameCueManager>(nullptr, *GlobalGameCueManagerName.ToString(), nullptr, LOAD_None, nullptr);
			if (GlobalGameCueManager == nullptr)
			{
				UE_LOG(LogTemp,Error, TEXT("Unable to Load GameCueManager %s"), *GlobalGameCueManagerName.ToString() );
			}
		}

		// Load specific GameCue manager class if specified
		if ( GlobalGameCueManager == nullptr && GlobalGameCueManagerClass.IsValid() )
		{
			UClass* GCMClass = LoadClass<UObject>(NULL, *GlobalGameCueManagerClass.ToString(), NULL, LOAD_None, NULL);
			if (GCMClass)
			{
				GlobalGameCueManager = NewObject<UGameCueManager>(this, GCMClass, NAME_None);
			}
		}

		if ( GlobalGameCueManager == nullptr)
		{
			// Fallback to base native class
			GlobalGameCueManager = NewObject<UGameCueManager>(this, UGameCueManager::StaticClass(), NAME_None);
		}

		GlobalGameCueManager->OnCreated();

		if (GameCueNotifyPaths.Num() == 0)
		{
			GameCueNotifyPaths.Add(TEXT("/Game"));
			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("No GameCueNotifyPaths were specified in DefaultGame.ini under [/Script/GMCAbilities.GMCAbilitySystemGlobals]. Falling back to using all of /Game/. This may be slow on large projects. Consider specifying which paths are to be searched."));

		}
		
		if (GlobalGameCueManager->ShouldAsyncLoadObjectLibrariesAtStart())
		{
			StartAsyncLoadingObjectLibraries();
		}
	}

	check(GlobalGameCueManager);
	return GlobalGameCueManager;

}


void UGMCAbilitySystemGlobals::AddGameCueNotifyPath(const FString& InPath)
{
	GameCueNotifyPaths.AddUnique(InPath);
}

int32 UGMCAbilitySystemGlobals::RemoveGameCueNotifyPath(const FString& InPath)
{
	return GameCueNotifyPaths.Remove(InPath);
}


void UGMCAbilitySystemGlobals::InitGameCueParameters(FGameCueParameters& CueParameters,
                                                         const FAbilityEffectSpecForRPC& Spec)
{

	CueParameters.AbilityLevel = Spec.GetAbilityLevel();
	
}

void UGMCAbilitySystemGlobals::InitGameCueParameters_GESpec(FGameCueParameters& CueParameters,
	const FAbilityEffectSpec& Spec)
{
}

void UGMCAbilitySystemGlobals::InitGameCueParameters(FGameCueParameters& CueParameters,
	const FAbilityEffectContextHandle& EffectContext)
{
	if (EffectContext.IsValid())
	{
		// Copy Context over wholesale. Projects may want to override this and not copy over all data
		
	}
}
