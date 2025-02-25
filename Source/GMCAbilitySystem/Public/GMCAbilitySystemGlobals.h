// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/SoftObjectPath.h"
#include "GameplayTagContainer.h"
#include "GMCAbilitySystem.h"
#include "Effects/GMCAbilityEffectTypes.h"
#include "GMCAbilitySystemGlobals.generated.h"

class UGameCueManager;
class UGMC_AbilitySystemComponent;
class UGameCueManager;
class UGameplayTagReponseTable;
struct FGameplayAbilityActorInfo;
struct FAbilityEffectSpec;
struct FAbilityEffectSpecForRPC;

/** Called when ability fails to activate, passes along the failed ability and a tag explaining why */
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAbilitySystemAssetOpenedDelegate, FString , int );
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAbilitySystemAssetFoundDelegate, FString, int);



/** Holds global data for the ability system. Can be configured per project via config file */
UCLASS(defaultconfig,config=Game)
class GMCABILITYSYSTEM_API UGMCAbilitySystemGlobals : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Gets the single instance of the globals object, will create it as necessary */
	static UGMCAbilitySystemGlobals& Get()
	{
		// Check if the singleton instance is valid
		if (!SingletonInstance)
		{
			// Optionally log to see if this is being triggered
			UE_LOG(LogTemp, Warning, TEXT("UGMCAbilitySystemGlobals singleton is being created"));

			// Initialize the singleton instance here
			SingletonInstance = NewObject<UGMCAbilitySystemGlobals>();
			SingletonInstance->AddToRoot(); // Prevent it from being garbage collected
		}

		// Make sure the singleton instance is valid
		check(SingletonInstance);

		return *SingletonInstance;
	}

	/** Will be called once on first use to load global data tables and tags (see FGameplayAbilitiesModule::GetAbilitySystemGlobals) */
	virtual void InitGlobalData();

	/** Returns true if InitGlobalData has been called */
	bool IsAbilitySystemGlobalsInitialized() const;

	virtual TArray<FString> GetGameCueNotifyPaths() { return GameCueNotifyPaths; }

	/**
	 * Trigger async loading of the gameplay cue object libraries. By default, the manager will do this on creation,
	 * but that behavior can be changed by a derived class overriding ShouldAsyncLoadObjectLibrariesAtStart and returning false.
	 * In that case, this function must be called to begin the load
	 */
	virtual void StartAsyncLoadingObjectLibraries();
	
	/** Searches the passed in actor for an ability system component, will use IAbilitySystemInterface or fall back to a component search */
	static UGMC_AbilitySystemComponent* GetAbilitySystemComponentFromActor(const AActor* Actor, bool LookForComponent=true);

	/** Should allocate a project specific AbilityActorInfo struct. Caller is responsible for deallocation */
	virtual FGameplayAbilityActorInfo* AllocAbilityActorInfo() const;

	/** Should allocate a project specific AbilityEffectContext struct. Caller is responsible for deallocation */
	virtual FAbilityEffectContext* AllocAbilityEffectContext() const;

	/** Class reference to gameplay cue manager. Use this if you want to just instantiate a class for your gameplay cue manager without having to create an asset. */
	UPROPERTY(config)
	FSoftObjectPath GlobalGameCueManagerClass;

	/** Object reference to gameplay cue manager (E.g., reference to a specific blueprint of your GameCueManager class. This is not necessary unless you want to have data or blueprints in your gameplay cue manager. */
	UPROPERTY(config)
	FSoftObjectPath GlobalGameCueManagerName;
	UPROPERTY()
	TObjectPtr<UGameCueManager> GlobalGameCueManager;

	/** Sets a default gameplay cue tag using the asset's name. Returns true if it changed the tag. */
	static bool DeriveGameCueTagFromAssetName(FString AssetName, FGameplayTag& GameCueTag, FName& GameCueName);

	/** Sets a default gameplay cue tag using the asset's class*/
	template<class T>
	static void DeriveGameCueTagFromClass(T* CDO)
	{
#if WITH_EDITOR
		UClass* ParentClass = CDO->GetClass()->GetSuperClass();
		if (T* ParentCDO = Cast<T>(ParentClass->GetDefaultObject()))
		{
			if (ParentCDO->GameCueTag.IsValid() && (ParentCDO->GameCueTag == CDO->GameCueTag))
			{
				// Parente has a valid tag. But maybe there is a better one for this class to use.
				// Reset our GameCueTag and see if we find one.
				FGameplayTag ParentTag = ParentCDO->GameCueTag;
				CDO->GameCueTag = FGameplayTag();
				if (UGMCAbilitySystemGlobals::DeriveGameCueTagFromAssetName(CDO->GetName(), CDO->GameCueTag, CDO->GameCueName) == false)
				{
					// We did not find one, so parent tag it is.
					CDO->GameCueTag = ParentTag;
				}
				return;
			}
		}
		UGMCAbilitySystemGlobals::DeriveGameCueTagFromAssetName(CDO->GetName(), CDO->GameCueTag, CDO->GameCueName);
#endif
	}
	
	/** Returns the gameplay cue manager singleton object, creating if necessary */
	virtual UGameCueManager* GetGameCueManager();
	void AddGameCueNotifyPath(const FString& InPath);
	int32 RemoveGameCueNotifyPath(const FString& InPath);
	void InitGameCueParameters(FGameCueParameters& CueParameters, const FAbilityEffectSpecForRPC& Spec);
	void InitGameCueParameters_GESpec(FGameCueParameters& CueParameters, const FAbilityEffectSpec& Spec);
	void InitGameCueParameters(FGameCueParameters& CueParameters, const FAbilityEffectContextHandle& EffectContext);


	/** Path where the engine will load gameplay cue notifies from */
	
	/** Look in these paths for GameCueNotifies. These are your "always loaded" set. */
	UPROPERTY(config)
	TArray<FString>	GameCueNotifyPaths;

	/** The class to instantiate as the globals object. Defaults to this class but can be overridden */
	UPROPERTY(config)
	FSoftClassPath AbilitySystemGlobalsClassName;



	/** Called when debug strings are available, to write them to the display */
	DECLARE_MULTICAST_DELEGATE(FOnClientServerDebugAvailable);
	FOnClientServerDebugAvailable OnClientServerDebugAvailable;

	/** Global place to accumulate debug strings for ability system component. Used when we fill up client side debug string immediately, and then wait for server to send server strings */
	TArray<FString>	AbilitySystemDebugStrings;

	/** Set to true if you want the "ShowDebug AbilitySystem" cheat to use the hud's debug target instead of the ability system's debug target. */
	UPROPERTY(config)
	bool bUseDebugTargetFromHud;


	// Global Tags

	/** TryActivate failed due to being dead */
	UPROPERTY()
	FGameplayTag ActivateFailIsDeadTag; 
	UPROPERTY(config)
	FName ActivateFailIsDeadName;

	/** TryActivate failed due to being on cooldown */
	UPROPERTY()
	FGameplayTag ActivateFailCooldownTag; 
	UPROPERTY(config)
	FName ActivateFailCooldownName;

	/** TryActivate failed due to not being able to spend costs */
	UPROPERTY()
	FGameplayTag ActivateFailCostTag; 
	UPROPERTY(config)
	FName ActivateFailCostName;

	/** TryActivate failed due to being blocked by other abilities */
	UPROPERTY()
	FGameplayTag ActivateFailTagsBlockedTag; 
	UPROPERTY(config)
	FName ActivateFailTagsBlockedName;

	/** TryActivate failed due to missing required tags */
	UPROPERTY()
	FGameplayTag ActivateFailTagsMissingTag; 
	UPROPERTY(config)
	FName ActivateFailTagsMissingName;

	/** Failed to activate due to invalid networking settings, this is designer error */
	UPROPERTY()
	FGameplayTag ActivateFailNetworkingTag; 
	UPROPERTY(config)
	FName ActivateFailNetworkingName;

	/** How many bits to use for "number of tags" in FMinimalReplicationTagCountMap::NetSerialize.  */
	UPROPERTY(config)
	int32	MinimalReplicationTagCountBits;

	/** Initialize global tags by reading from config using the names and creating tags for use at runtime */
	virtual void InitGlobalTags()
	{
		if (ActivateFailIsDeadName != NAME_None)
		{
			ActivateFailIsDeadTag = FGameplayTag::RequestGameplayTag(ActivateFailIsDeadName);
		}

		if (ActivateFailCooldownName != NAME_None)
		{
			ActivateFailCooldownTag = FGameplayTag::RequestGameplayTag(ActivateFailCooldownName);
		}

		if (ActivateFailCostName != NAME_None)
		{
			ActivateFailCostTag = FGameplayTag::RequestGameplayTag(ActivateFailCostName);
		}

		if (ActivateFailTagsBlockedName != NAME_None)
		{
			ActivateFailTagsBlockedTag = FGameplayTag::RequestGameplayTag(ActivateFailTagsBlockedName);
		}

		if (ActivateFailTagsMissingName != NAME_None)
		{
			ActivateFailTagsMissingTag = FGameplayTag::RequestGameplayTag(ActivateFailTagsMissingName);
		}

		if (ActivateFailNetworkingName != NAME_None)
		{
			ActivateFailNetworkingTag = FGameplayTag::RequestGameplayTag(ActivateFailNetworkingName);
		}
	}

	void InitTargetDataScriptStructCache();


	static UGMCAbilitySystemGlobals* SingletonInstance;

};


