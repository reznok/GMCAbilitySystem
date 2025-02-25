// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "Engine/NetSerialization.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Effects/GMCAbilityEffectTypes.h"
#include "GameplayPrediction.h"
#include "GMC_GameCueInterface.generated.h"

#if UE_WITH_IRIS
struct FMinimalGameCueReplicationProxyForNetSerializer;
namespace UE::Net
{
	class FMinimalGameCueReplicationProxyReplicationFragment;
}
#endif

/** Interface for actors that wish to handle GameCue events from GameplayEffects. Native only because blueprints can't implement interfaces with native functions */
UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UGameCueInterface: public UInterface
{
	GENERATED_UINTERFACE_BODY()
};

class GMCABILITYSYSTEM_API IGameCueInterface
{
	GENERATED_IINTERFACE_BODY()

public:

	/** Handle a single gameplay cue */
	virtual void HandleGameCue(UObject* Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Wrapper that handles multiple cues */
	virtual void HandleGameCues(UObject* Self, const FGameplayTagContainer& GameCueTags, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/**
	* Returns true if the object can currently accept gameplay cues associated with the given tag. Returns true by default.
	* Allows objects to opt out of cues in cases such as pending death
	*/
	virtual bool ShouldAcceptGameCue(UObject* Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);


	// DEPRECATED - use the UObject* signatures above

	/** Handle a single gameplay cue */
	virtual void HandleGameCue(AActor *Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Wrapper that handles multiple cues */
	virtual void HandleGameCues(AActor *Self, const FGameplayTagContainer& GameCueTags, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Returns true if the actor can currently accept gameplay cues associated with the given tag. Returns true by default. Allows actors to opt out of cues in cases such as pending death */
	virtual bool ShouldAcceptGameCue(AActor *Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	// END DEPRECATED


	/** Return the cue sets used by this object. This is optional and it is possible to leave this list empty. */
	virtual void GetGameCueSets(TArray<class UGameCueSet*>& OutSets) const {}

	/** Default native handler, called if no tag matches found */
	virtual void GameCueDefaultHandler(EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Internal function to map ufunctions directly to GameCue tags */
	UFUNCTION(BlueprintImplementableEvent, BlueprintCosmetic, Category = GameCue, meta = (BlueprintInternalUseOnly = "true"))
	void BlueprintCustomHandler(EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Call from a Cue handler event to continue checking for additional, more generic handlers. Called from the ability system blueprint library */
	UFUNCTION(BlueprintCallable, BlueprintCosmetic, Category="Ability|GameCue")
	virtual void ForwardGameCueToParent();

	/** Calls the UFunction override for a specific gameplay cue */
	static void DispatchBlueprintCustomHandler(UObject* Object, UFunction* Func, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	/** Clears internal cache of what classes implement which functions */
	static void ClearTagToFunctionMap();

	IGameCueInterface() : bForwardToParent(false) {}

private:
	/** If true, keep checking for additional handlers */
	bool bForwardToParent;
};


/**
 *	This is meant to provide another way of using GameCues without having to go through GameplayEffects.
 *	E.g., it is convenient if GameplayAbilities can issue replicated GameCues without having to create
 *	a GameplayEffect.
 *	
 *	Essentially provides bare necessities to replicate GameCue Tags.
 */
struct FActiveGameCueContainer;

USTRUCT(BlueprintType)
struct FActiveGameCue : public FFastArraySerializerItem
{
	GENERATED_USTRUCT_BODY()

	FActiveGameCue()	
	{
		bPredictivelyRemoved = false;
	}

	UPROPERTY()
	FGameplayTag GameCueTag;

	UPROPERTY()
	FPredictionKey PredictionKey;

	UPROPERTY()
	FGameCueParameters Parameters;

	/** Has this been predictively removed on the client? */
	UPROPERTY(NotReplicated)
	bool bPredictivelyRemoved;

	void PreReplicatedRemove(const struct FActiveGameCueContainer &InArray);
	void PostReplicatedAdd(const struct FActiveGameCueContainer &InArray);
	void PostReplicatedChange(const struct FActiveGameCueContainer &InArray) { }

	FString GetDebugString();
};

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FActiveGameCueContainer : public FFastArraySerializer
{
	GENERATED_USTRUCT_BODY()

	UPROPERTY()
	TArray< FActiveGameCue >	GameCues;

	void SetOwner(UGMC_AbilitySystemComponent* InOwner);

	/** Should this container only replicate in minimal replication mode */
	bool bMinimalReplication;

	void AddCue(const FGameplayTag& Tag, const FPredictionKey& PredictionKey, const FGameCueParameters& Parameters);
	void RemoveCue(const FGameplayTag& Tag);

	/** Marks as predictively removed so that we dont invoke remove event twice due to onrep */
	void PredictiveRemove(const FGameplayTag& Tag);

	void PredictiveAdd(const FGameplayTag& Tag, FPredictionKey& PredictionKey);

	/** Does explicit check for gameplay cue tag */
	bool HasCue(const FGameplayTag& Tag) const;

	/** Returns true if the instance should be replicated. If false the property is allowed to be disabled for replication. */
	bool ShouldReplicate() const;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms);

	// Will broadcast the OnRemove event for all currently active cues
	void RemoveAllCues();

private:

	int32 GetGameStateTime(const UWorld* World) const;

	UPROPERTY(NotReplicated)
	TObjectPtr<class UGMC_AbilitySystemComponent>	Owner = nullptr;
	
	friend struct FActiveGameCue;
};

template<>
struct TStructOpsTypeTraits< FActiveGameCueContainer > : public TStructOpsTypeTraitsBase2< FActiveGameCueContainer >
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};


/**
 *	Wrapper struct around a gameplaytag with the GameCue category. This also allows for a details customization
 */
USTRUCT(BlueprintType)
struct FGameCueTag
{
	GENERATED_USTRUCT_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (Categories="GameCue"), Category="GameCue")
	FGameplayTag GameCueTag;

	bool IsValid() const
	{
		return GameCueTag.IsValid();
	}
};

/** 
 *	An alternative way to replicating gameplay cues. This does not use fast TArray serialization and does not serialize GameCue parameters. The parameters are created on the receiving side with default information.
 *	This will be more efficient with server cpu but will take more bandwidth when the array changes.
 *	
 *	To use, put this on your replication proxy actor (such a the pawn). Call SetOwner, PreReplication and RemoveallCues in the appropriate places.
 */
USTRUCT()
struct GMCABILITYSYSTEM_API FMinimalGameCueReplicationProxy
{
	GENERATED_BODY()

	FMinimalGameCueReplicationProxy();

	/** Set Owning ASC. This is what the GC callbacks are called on.  */
	void SetOwner(UGMC_AbilitySystemComponent* ASC);

	/** Copies data in from an FActiveGameCueContainer (such as the one of the ASC). You must call this manually from PreReplication. */
	void PreReplication(const FActiveGameCueContainer& SourceContainer);

	/** Custom NetSerialization to pack the entire array */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Will broadcast the OnRemove event for all currently active cues */
	void RemoveAllCues();

	/** If true, we will skip updating the Owner ASC if we replicate on a connection owned by the ASC */
	void SetRequireNonOwningNetConnection(bool b) { bRequireNonOwningNetConnection = b; }

	/** Called to init parameters */
	TFunction<void(FGameCueParameters&, UGMC_AbilitySystemComponent*)> InitGameCueParametersFunc;

	bool operator==(const FMinimalGameCueReplicationProxy& Other) const { return LastSourceArrayReplicationKey == Other.LastSourceArrayReplicationKey; }
	bool operator!=(const FMinimalGameCueReplicationProxy& Other) const { return !(*this == Other); }

private:
#if UE_WITH_IRIS
	friend FMinimalGameCueReplicationProxyForNetSerializer;
	friend UE::Net::FMinimalGameCueReplicationProxyReplicationFragment;
#endif

	enum { NumInlineTags = 16 };

	TArray< FGameplayTag, TInlineAllocator<NumInlineTags> >	ReplicatedTags;
	TArray< FVector_NetQuantize, TInlineAllocator<NumInlineTags> > ReplicatedLocations;
	TArray< FGameplayTag, TInlineAllocator<NumInlineTags> >	LocalTags;
	TBitArray< TInlineAllocator<NumInlineTags> >			LocalBitMask;

	UPROPERTY()
	TObjectPtr<UGMC_AbilitySystemComponent> Owner = nullptr;

	int32 LastSourceArrayReplicationKey = -1;

	bool bRequireNonOwningNetConnection = false;
	bool bCachedModifiedOwnerTags = false;
};

template<>
struct TStructOpsTypeTraits< FMinimalGameCueReplicationProxy > : public TStructOpsTypeTraitsBase2< FMinimalGameCueReplicationProxy >
{
	enum
	{
		WithNetSerializer = true,
		WithNetSharedSerialization = true,
		WithIdenticalViaEquality = true,
	};
};