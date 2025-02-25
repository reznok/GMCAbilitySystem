#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/ObjectMacros.h"
#include "Engine/NetSerialization.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

#include "GMCAbilityEffectTypes.generated.h"

class UGMCAbility;
class UGMC_AbilitySystemComponent;
class UGMCAbilityEffect;

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAbilityEffectTagCountChanged, const FGameplayTag, int32);
DECLARE_DELEGATE(FDeferredTagChangeDelegate);


DECLARE_MULTICAST_DELEGATE_ThreeParams(FOnActiveAbilityEffectStackChange, int32, int32, int32);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnActiveAbilityEffectRemoved, const FAbilityEffectRemovalInfo&);




UENUM(BlueprintType)
namespace EGameplayTagEventType
{
	/** Rather a tag was added or removed, used in callbacks */
	enum Type : int
	{		
		/** Event only happens when tag is new or completely removed */
		NewOrRemoved,

		/** Event happens any time tag "count" changes */
		AnyCountChange		
	};
}


UENUM(BlueprintType)
namespace EGameCueEvent
{
	/** Indicates what type of action happened to a specific gameplay cue tag. Sometimes you will get multiple events at once */
	enum Type : int
	{
		/** Called when a GameCue with duration is first activated, this will only be called if the client witnessed the activation */
		OnActive,

		/** Called when a GameCue with duration is first seen as active, even if it wasn't actually just applied (Join in progress, etc) */
		WhileActive,

		/** Called when a GameCue is executed, this is used for instant effects or periodic ticks */
		Executed,

		/** Called when a GameCue with duration is removed */
		Removed
	};
}


struct GMCABILITYSYSTEM_API FGameplayTagCountContainer
{	
	FGameplayTagCountContainer()
	{}

	/**
	 * Check if the count container has a gameplay tag that matches against the specified tag (expands to include parents of asset tags)
	 * 
	 * @param TagToCheck	Tag to check for a match
	 * 
	 * @return True if the count container has a gameplay tag that matches, false if not
	 */
	FORCEINLINE bool HasMatchingGameplayTag(FGameplayTag TagToCheck) const
	{
		return GameplayTagCountMap.FindRef(TagToCheck) > 0;
	}

	/**
	 * Check if the count container has gameplay tags that matches against all of the specified tags (expands to include parents of asset tags)
	 * 
	 * @param TagContainer			Tag container to check for a match. If empty will return true
	 * 
	 * @return True if the count container matches all of the gameplay tags
	 */
	FORCEINLINE bool HasAllMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
	{
		// if the TagContainer count is 0 return bCountEmptyAsMatch;
		if (TagContainer.Num() == 0)
		{
			return true;
		}

		bool AllMatch = true;
		for (const FGameplayTag& Tag : TagContainer)
		{
			if (GameplayTagCountMap.FindRef(Tag) <= 0)
			{
				AllMatch = false;
				break;
			}
		}		
		return AllMatch;
	}
	
	/**
	 * Check if the count container has gameplay tags that matches against any of the specified tags (expands to include parents of asset tags)
	 * 
	 * @param TagContainer			Tag container to check for a match. If empty will return false
	 * 
	 * @return True if the count container matches any of the gameplay tags
	 */
	FORCEINLINE bool HasAnyMatchingGameplayTags(const FGameplayTagContainer& TagContainer) const
	{
		if (TagContainer.Num() == 0)
		{
			return false;
		}

		bool AnyMatch = false;
		for (const FGameplayTag& Tag : TagContainer)
		{
			if (GameplayTagCountMap.FindRef(Tag) > 0)
			{
				AnyMatch = true;
				break;
			}
		}
		return AnyMatch;
	}
	
	/**
	 * Update the specified container of tags by the specified delta, potentially causing an additional or removal from the explicit tag list
	 * 
	 * @param Container		Container of tags to update
	 * @param CountDelta	Delta of the tag count to apply
	 */
	FORCEINLINE void UpdateTagCount(const FGameplayTagContainer& Container, int32 CountDelta)
	{
		if (CountDelta != 0)
		{
			bool bUpdatedAny = false;
			TArray<FDeferredTagChangeDelegate> DeferredTagChangeDelegates;
			for (auto TagIt = Container.CreateConstIterator(); TagIt; ++TagIt)
			{
				bUpdatedAny |= UpdateTagMapDeferredParentRemoval_Internal(*TagIt, CountDelta, DeferredTagChangeDelegates);
			}

			if (bUpdatedAny && CountDelta < 0)
			{
				ExplicitTags.FillParentTags();
			}

			for (FDeferredTagChangeDelegate& Delegate : DeferredTagChangeDelegates)
			{
				Delegate.Execute();
			}
		}
	}
	
	/**
	 * Update the specified tag by the specified delta, potentially causing an additional or removal from the explicit tag list
	 * 
	 * @param Tag						Tag to update
	 * @param CountDelta				Delta of the tag count to apply
	 * 
	 * @return True if tag was *either* added or removed. (E.g., we had the tag and now dont. or didnt have the tag and now we do. We didn't just change the count (1 count -> 2 count would return false).
	 */
	FORCEINLINE bool UpdateTagCount(const FGameplayTag& Tag, int32 CountDelta)
	{
		if (CountDelta != 0)
		{
			return UpdateTagMap_Internal(Tag, CountDelta);
		}

		return false;
	}

	/**
	 * Update the specified tag by the specified delta, potentially causing an additional or removal from the explicit tag list.
	 * Calling code MUST call FillParentTags followed by executing the returned delegates.
	 * 
	 * @param Tag						Tag to update
	 * @param CountDelta				Delta of the tag count to apply
	 * @param DeferredTagChangeDelegates		Delegates to be called after this code runs
	 * 
	 * @return True if tag was *either* added or removed. (E.g., we had the tag and now dont. or didnt have the tag and now we do. We didn't just change the count (1 count -> 2 count would return false).
	 */
	FORCEINLINE bool UpdateTagCount_DeferredParentRemoval(const FGameplayTag& Tag, int32 CountDelta, TArray<FDeferredTagChangeDelegate>& DeferredTagChangeDelegates)
	{
		if (CountDelta != 0)
		{
			return UpdateTagMapDeferredParentRemoval_Internal(Tag, CountDelta, DeferredTagChangeDelegates);
		}

		return false;
	}

	/**
	 * Set the specified tag count to a specific value
	 * 
	 * @param Tag			Tag to update
	 * @param Count			New count of the tag
	 * 
	 * @return True if tag was *either* added or removed. (E.g., we had the tag and now dont. or didnt have the tag and now we do. We didn't just change the count (1 count -> 2 count would return false).
	 */
	FORCEINLINE bool SetTagCount(const FGameplayTag& Tag, int32 NewCount)
	{
		int32 ExistingCount = 0;
		if (int32* Ptr  = ExplicitTagCountMap.Find(Tag))
		{
			ExistingCount = *Ptr;
		}

		int32 CountDelta = NewCount - ExistingCount;
		if (CountDelta != 0)
		{
			return UpdateTagMap_Internal(Tag, CountDelta);
		}

		return false;
	}

	/**
	* return the count for a specified tag 
	*
	* @param Tag			Tag to update
	*
	* @return the count of the passed in tag
	*/
	FORCEINLINE int32 GetTagCount(const FGameplayTag& Tag) const
	{
		if (const int32* Ptr = GameplayTagCountMap.Find(Tag))
		{
			return *Ptr;
		}

		return 0;
	}

	/**
	 *	Broadcasts the AnyChange event for this tag. This is called when the stack count of the backing gameplay effect change.
	 *	It is up to the receiver of the broadcasted delegate to decide what to do with this.
	 */
	void Notify_StackCountChange(const FGameplayTag& Tag);

	/**
	 * Return delegate that can be bound to for when the specific tag's count changes to or off of zero
	 *
	 * @param Tag	Tag to get a delegate for
	 * 
	 * @return Delegate for when the specified tag's count changes to or off of zero
	 */
	FOnAbilityEffectTagCountChanged& RegisterGameplayTagEvent(const FGameplayTag& Tag, EGameplayTagEventType::Type EventType=EGameplayTagEventType::NewOrRemoved);
	
	/**
	 * Return delegate that can be bound to for when the any tag's count changes to or off of zero
	 * 
	 * @return Delegate for when any tag's count changes to or off of zero
	 */
	FOnAbilityEffectTagCountChanged& RegisterGenericGameplayEvent()
	{
		return OnAnyTagChangeDelegate;
	}

	/** Simple accessor to the explicit gameplay tag list */
	const FGameplayTagContainer& GetExplicitGameplayTags() const
	{
		return ExplicitTags;
	}

	void Reset();

	/** Fills in ParentTags from GameplayTags */
	void FillParentTags()
	{
		ExplicitTags.FillParentTags();
	}

private:

	struct FDelegateInfo
	{
		FOnAbilityEffectTagCountChanged	OnNewOrRemove;
		FOnAbilityEffectTagCountChanged	OnAnyChange;
	};

	/** Map of tag to delegate that will be fired when the count for the key tag changes to or away from zero */
	TMap<FGameplayTag, FDelegateInfo> GameplayTagEventMap;

	/** Map of tag to active count of that tag */
	TMap<FGameplayTag, int32> GameplayTagCountMap;

	/** Map of tag to explicit count of that tag. Cannot share with above map because it's not safe to merge explicit and generic counts */	
	TMap<FGameplayTag, int32> ExplicitTagCountMap;

	/** Delegate fired whenever any tag's count changes to or away from zero */
	FOnAbilityEffectTagCountChanged OnAnyTagChangeDelegate;

	/** Container of tags that were explicitly added */
	FGameplayTagContainer ExplicitTags;

	/** Internal helper function to adjust the explicit tag list & corresponding maps/delegates/etc. as necessary */
	bool UpdateTagMap_Internal(const FGameplayTag& Tag, int32 CountDelta);

	/** Internal helper function to adjust the explicit tag list & corresponding maps/delegates/etc. as necessary. This does not call FillParentTags or any of the tag change delegates. These delegates are returned and must be executed by the caller. */
	bool UpdateTagMapDeferredParentRemoval_Internal(const FGameplayTag& Tag, int32 CountDelta, TArray<FDeferredTagChangeDelegate>& DeferredTagChangeDelegates);

	/** Internal helper function to adjust the explicit tag list & corresponding map. */
	bool UpdateExplicitTags(const FGameplayTag& Tag, int32 CountDelta, bool bDeferParentTagsOnRemove);

	/** Internal helper function to collect the delegates that need to be called when Tag has its count changed by CountDelta. */
	bool GatherTagChangeDelegates(const FGameplayTag& Tag, int32 CountDelta, TArray<FDeferredTagChangeDelegate>& TagChangeDelegates);
};



USTRUCT()
struct GMCABILITYSYSTEM_API FAbilityEffectContext
{
	GENERATED_USTRUCT_BODY()

	FAbilityEffectContext()
	: AbilityLevel(1)
	, WorldOrigin(ForceInitToZero)
	, bHasWorldOrigin(false)
	, bReplicateSourceObject(false)
	, bReplicateInstigator(false)
	, bReplicateEffectCauser(false)
	{
	}

	FAbilityEffectContext(AActor* InInstigator, AActor* InEffectCauser)
	: AbilityLevel(1)
	, WorldOrigin(ForceInitToZero)
	, bHasWorldOrigin(false)
	, bReplicateSourceObject(false)
	, bReplicateInstigator(false)
	, bReplicateEffectCauser(false)
	{
		FAbilityEffectContext::AddInstigator(InInstigator, InEffectCauser);
	}

	virtual ~FAbilityEffectContext()
	{
	}
	/** Returns the list of gameplay tags applicable to this effect, defaults to the owner's tags. SpecTagContainer remains untouched by default. */

	virtual void GetOwnedGameplayTags(OUT FGameplayTagContainer& ActorTagContainer, OUT FGameplayTagContainer& SpecTagContainer) const;

	/** Sets the instigator and effect causer. Instigator is who owns the ability that spawned this, EffectCauser is the actor that is the physical source of the effect, such as a weapon. They can be the same. */
	virtual void AddInstigator(class AActor *InInstigator, class AActor *InEffectCauser);

	/** Sets the ability that was used to spawn this */
	virtual void SetAbility(const UGMCAbility* InGameplayAbility);

	/** Returns the immediate instigator that applied this effect */
	virtual AActor* GetInstigator() const
	{
		return Instigator.Get();
	}

	/** Returns the CDO of the ability used to instigate this context */
	const UGMCAbility* GetAbility() const;

	/** Returns the specific instance that instigated this, may not always be set */
	const UGMCAbility* GetAbilityInstance_NotReplicated() const;

	/** Gets the ability level this was evaluated at */
	int32 GetAbilityLevel() const
	{
		return AbilityLevel;
	}

	/** Returns the ability system component of the instigator of this effect */
	virtual UGMC_AbilitySystemComponent* GetInstigatorAbilitySystemComponent() const
	{
		return InstigatorAbilitySystemComponent.Get();
	}

	/** Returns the physical actor tied to the application of this effect */
	virtual AActor* GetEffectCauser() const
	{
		return EffectCauser.Get();
	}

	/** Modify the effect causer actor, useful when that information is added after creation */
	void SetEffectCauser(AActor* InEffectCauser)
	{
		EffectCauser = InEffectCauser;
		bReplicateEffectCauser = CanActorReferenceBeReplicated(InEffectCauser);
	}

	/** Should always return the original instigator that started the whole chain. Subclasses can override what this does */
	virtual AActor* GetOriginalInstigator() const
	{
		return Instigator.Get();
	}

	/** Returns the ability system component of the instigator that started the whole chain */
	virtual UGMC_AbilitySystemComponent* GetOriginalInstigatorAbilitySystemComponent() const
	{
		return InstigatorAbilitySystemComponent.Get();
	}

	/** Sets the object this effect was created from. */
	virtual void AddSourceObject(const UObject* NewSourceObject)
	{
		SourceObject = MakeWeakObjectPtr(const_cast<UObject*>(NewSourceObject));
		bReplicateSourceObject = NewSourceObject && NewSourceObject->IsSupportedForNetworking();
	}

	/** Returns the object this effect was created from. */
	virtual UObject* GetSourceObject() const
	{
		return SourceObject.Get();
	}

	/** Add actors to the stored actor list */
	virtual void AddActors(const TArray<TWeakObjectPtr<AActor>>& IActor, bool bReset = false);

	/** Add a hit result for targeting */
	virtual void AddHitResult(const FHitResult& InHitResult, bool bReset = false);

	/** Returns actor list, may be empty */
	virtual const TArray<TWeakObjectPtr<AActor>>& GetActors() const
	{
		return Actors;
	}

	/** Returns hit result, this can be null */
	virtual const FHitResult* GetHitResult() const
	{
		return const_cast<FAbilityEffectContext*>(this)->GetHitResult();
	}

	/** Returns hit result, this can be null */
	virtual FHitResult* GetHitResult()
	{
		return HitResult.Get();
	}

	/** Adds an origin point */
	virtual void AddOrigin(FVector InOrigin);

	/** Returns origin point, may be invalid if HasOrigin is false */
	virtual const FVector& GetOrigin() const
	{
		return WorldOrigin;
	}

	/** Returns true if GetOrigin will give valid information */
	virtual bool HasOrigin() const
	{
		return bHasWorldOrigin;
	}

	/** Returns debug string */
	virtual FString ToString() const;

	/** Returns the actual struct used for serialization, subclasses must override this! */
	virtual UScriptStruct* GetScriptStruct() const
	{
		return FAbilityEffectContext::StaticStruct();
	}

	/** Creates a copy of this context, used to duplicate for later modifications */
	virtual FAbilityEffectContext* Duplicate() const
	{
		FAbilityEffectContext* NewContext = new FAbilityEffectContext();
		*NewContext = *this;
		if (GetHitResult())
		{
			// Does a deep copy of the hit result
			NewContext->AddHitResult(*GetHitResult(), true);
		}
		return NewContext;
	}

	/** True if this was instigated by a locally controlled actor */
	virtual bool IsLocallyControlled() const;

	/** True if this was instigated by a locally controlled player */
	virtual bool IsLocallyControlledPlayer() const;

	/** Custom serialization, subclasses must override this */
	virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

protected:
	static bool CanActorReferenceBeReplicated(const AActor* Actor);

	// The object pointers here have to be weak because contexts aren't necessarily tracked by GC in all cases

	/** Instigator actor, the actor that owns the ability system component */
	UPROPERTY()
	TWeakObjectPtr<AActor> Instigator;

	/** The physical actor that actually did the damage, can be a weapon or projectile */
	UPROPERTY()
	TWeakObjectPtr<AActor> EffectCauser;

	/** The ability CDO that is responsible for this effect context (replicated) */
	UPROPERTY()
	TWeakObjectPtr<UGMCAbility> AbilityCDO;

	/** The ability instance that is responsible for this effect context (NOT replicated) */
	UPROPERTY(NotReplicated)
	TWeakObjectPtr<UGMCAbility> AbilityInstanceNotReplicated;

	/** The level this was executed at */
	UPROPERTY()
	int32 AbilityLevel;

	/** Object this effect was created from, can be an actor or static object. Useful to bind an effect to a gameplay object */
	UPROPERTY()
	TWeakObjectPtr<UObject> SourceObject;

	/** The ability system component that's bound to instigator */
	UPROPERTY(NotReplicated)
	TWeakObjectPtr<UGMC_AbilitySystemComponent> InstigatorAbilitySystemComponent;

	/** Actors referenced by this context */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> Actors;

	/** Trace information - may be nullptr in many cases */
	TSharedPtr<FHitResult>	HitResult;

	/** Stored origin, may be invalid if bHasWorldOrigin is false */
	UPROPERTY()
	FVector	WorldOrigin;

	UPROPERTY()
	uint8 bHasWorldOrigin:1;

	/** True if the SourceObject can be replicated. This bool is not replicated itself. */
	UPROPERTY(NotReplicated)
	uint8 bReplicateSourceObject:1;
	
	/** True if the Instigator can be replicated. This bool is not replicated itself. */
	UPROPERTY(NotReplicated)	
	uint8 bReplicateInstigator:1;

	/** True if the Instigator can be replicated. This bool is not replicated itself. */
	UPROPERTY(NotReplicated)	
	uint8 bReplicateEffectCauser:1;
};

template<>
struct TStructOpsTypeTraits< FAbilityEffectContext > : public TStructOpsTypeTraitsBase2< FAbilityEffectContext >
{
	enum
	{
		WithNetSerializer = true,
		WithCopy = true		// Necessary so that TSharedPtr<FHitResult> Data is copied around
	};
};


USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FAbilityEffectContextHandle
{
	GENERATED_USTRUCT_BODY()

	FAbilityEffectContextHandle()
	{
	}

	virtual ~FAbilityEffectContextHandle()
	{
	}

	/** Constructs from an existing context, should be allocated by new */
	explicit FAbilityEffectContextHandle(FAbilityEffectContext* DataPtr)
	{
		Data = TSharedPtr<FAbilityEffectContext>(DataPtr);
	}

	/** Sets from an existing context, should be allocated by new */
	void operator=(FAbilityEffectContext* DataPtr)
	{
		Data = TSharedPtr<FAbilityEffectContext>(DataPtr);
	}

	void Clear()
	{
		Data.Reset();
	}

	bool IsValid() const
	{
		return Data.IsValid();
	}

	/** Returns Raw effet context, may be null */
	FAbilityEffectContext* Get()
	{
		return IsValid() ? Data.Get() : nullptr;
	}
	const FAbilityEffectContext* Get() const
	{
		return IsValid() ? Data.Get() : nullptr;
	}

	/** Returns the list of gameplay tags applicable to this effect, defaults to the owner's tags */
	void GetOwnedGameplayTags(OUT FGameplayTagContainer& ActorTagContainer, OUT FGameplayTagContainer& SpecTagContainer) const
	{
		if (IsValid())
		{
			Data->GetOwnedGameplayTags(ActorTagContainer, SpecTagContainer);
		}
	}

	/** Sets the instigator and effect causer. Instigator is who owns the ability that spawned this, EffectCauser is the actor that is the physical source of the effect, such as a weapon. They can be the same. */
	void AddInstigator(class AActor *InInstigator, class AActor *InEffectCauser)
	{
		if (IsValid())
		{
			Data->AddInstigator(InInstigator, InEffectCauser);
		}
	}

	/** Sets Ability instance and CDO parameters on context */
	void SetAbility(const UGMCAbility* InGameplayAbility)
	{
		if (IsValid())
		{
			Data->SetAbility(InGameplayAbility);
		}
	}

	/** Returns the immediate instigator that applied this effect */
	virtual AActor* GetInstigator() const
	{
		if (IsValid())
		{
			return Data->GetInstigator();
		}
		return nullptr;
	}

	/** Returns the Ability CDO */
	const UGMCAbility* GetAbility() const
	{
		if (IsValid())
		{
			return Data->GetAbility();
		}
		return nullptr;
	}

	/** Returns the Ability Instance (never replicated) */
	const UGMCAbility* GetAbilityInstance_NotReplicated() const
	{
		if (IsValid())
		{
			return Data->GetAbilityInstance_NotReplicated();
		}
		return nullptr;
	}

	/** Returns level this was executed at */
	int32 GetAbilityLevel() const
	{
		if (IsValid())
		{
			return Data->GetAbilityLevel();
		}
		return 1;
	}

	/** Returns the ability system component of the instigator of this effect */
	virtual UGMC_AbilitySystemComponent* GetInstigatorAbilitySystemComponent() const
	{
		if (IsValid())
		{
			return Data->GetInstigatorAbilitySystemComponent();
		}
		return nullptr;
	}

	/** Returns the physical actor tied to the application of this effect */
	virtual AActor* GetEffectCauser() const
	{
		if (IsValid())
		{
			return Data->GetEffectCauser();
		}
		return nullptr;
	}

	/** Should always return the original instigator that started the whole chain. Subclasses can override what this does */
	AActor* GetOriginalInstigator() const
	{
		if (IsValid())
		{
			return Data->GetOriginalInstigator();
		}
		return nullptr;
	}

	/** Returns the ability system component of the instigator that started the whole chain */
	UGMC_AbilitySystemComponent* GetOriginalInstigatorAbilitySystemComponent() const
	{
		if (IsValid())
		{
			return Data->GetOriginalInstigatorAbilitySystemComponent();
		}
		return nullptr;
	}

	/** Sets the object this effect was created from. */
	void AddSourceObject(const UObject* NewSourceObject)
	{
		if (IsValid())
		{
			Data->AddSourceObject(NewSourceObject);
		}
	}

	/** Returns the object this effect was created from. */
	UObject* GetSourceObject() const
	{
		if (IsValid())
		{
			return Data->GetSourceObject();
		}
		return nullptr;
	}

	/** Returns if the instigator is locally controlled */
	bool IsLocallyControlled() const
	{
		if (IsValid())
		{
			return Data->IsLocallyControlled();
		}
		return false;
	}

	/** Returns if the instigator is locally controlled and a player */
	bool IsLocallyControlledPlayer() const
	{
		if (IsValid())
		{
			return Data->IsLocallyControlledPlayer();
		}
		return false;
	}

	/** Add actors to the stored actor list */
	void AddActors(const TArray<TWeakObjectPtr<AActor>>& InActors, bool bReset = false)
	{
		if (IsValid())
		{
			Data->AddActors(InActors, bReset);
		}
	}

	/** Add a hit result for targeting */
	void AddHitResult(const FHitResult& InHitResult, bool bReset = false)
	{
		if (IsValid())
		{
			Data->AddHitResult(InHitResult, bReset);
		}
	}

	/** Returns actor list, may be empty */
	const TArray<TWeakObjectPtr<AActor>> GetActors()
	{
		if (IsValid())
		{
			return Data->GetActors();
		}

		return {};
	}

	/** Returns hit result, this can be null */
	const FHitResult* GetHitResult() const
	{
		if (IsValid())
		{
			return Data->GetHitResult();
		}
		return nullptr;
	}

	/** Adds an origin point */
	void AddOrigin(FVector InOrigin)
	{
		if (IsValid())
		{
			Data->AddOrigin(InOrigin);
		}
	}

	/** Returns origin point, may be invalid if HasOrigin is false */
	virtual const FVector& GetOrigin() const
	{
		if (IsValid())
		{
			return Data->GetOrigin();
		}
		return FVector::ZeroVector;
	}

	/** Returns true if GetOrigin will give valid information */
	virtual bool HasOrigin() const
	{
		if (IsValid())
		{
			return Data->HasOrigin();
		}
		return false;
	}

	/** Returns debug string */
	FString ToString() const
	{
		return IsValid() ? Data->ToString() : FString(TEXT("NONE"));
	}

	/** Creates a deep copy of this handle, used before modifying */
	FAbilityEffectContextHandle Duplicate() const
	{
		if (IsValid())
		{
			FAbilityEffectContext* NewContext = Data->Duplicate();
			return FAbilityEffectContextHandle(NewContext);
		}
		else
		{
			return FAbilityEffectContextHandle();
		}
	}

	/** Comparison operator */
	bool operator==(FAbilityEffectContextHandle const& Other) const
	{
		if (Data.IsValid() != Other.Data.IsValid())
		{
			return false;
		}
		if (Data.Get() != Other.Data.Get())
		{
			return false;
		}
		return true;
	}

	/** Comparison operator */
	bool operator!=(FAbilityEffectContextHandle const& Other) const
	{
		return !(FAbilityEffectContextHandle::operator==(Other));
	}

	/** Custom serializer, handles polymorphism of context */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

private:

	TSharedPtr<FAbilityEffectContext> Data;
};

template<>
struct TStructOpsTypeTraits<FAbilityEffectContextHandle> : public TStructOpsTypeTraitsBase2<FAbilityEffectContextHandle>
{
	enum
	{
		WithCopy = true,		// Necessary so that TSharedPtr<FAbilityEffectContext> Data is copied around
		WithNetSerializer = true,
		WithIdenticalViaEquality = true,
	};
};



/** Metadata about a gameplay cue execution */
//USTRUCT(BlueprintType, meta = (HasNativeBreak = "/Script/GameplayAbilities.AbilitySystemBlueprintLibrary.BreakGameCueParameters", HasNativeMake = "/Script/GameplayAbilities.AbilitySystemBlueprintLibrary.MakeGameCueParameters"))

USTRUCT(BlueprintType)
struct GMCABILITYSYSTEM_API FGameCueParameters
{
	GENERATED_USTRUCT_BODY()

	FGameCueParameters()
	: NormalizedMagnitude(0.0f)
	, RawMagnitude(0.0f)
	, Location(ForceInitToZero)
	, Normal(ForceInitToZero)
	, AbilityEffectLevel(1)
	, AbilityLevel(1)
	{}

	/** Projects can override this via UAbilitySystemGlobals */
	FGameCueParameters(const struct FAbilityEffectSpecForRPC &Spec);
	FGameCueParameters(const struct FAbilityEffectContextHandle& AbilityEffectContext);

	/** Magnitude of source gameplay effect, normalzed from 0-1. Use this for "how strong is the gameplay effect" (0=min, 1=,max) */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	float NormalizedMagnitude;

	/** Raw final magnitude of source gameplay effect. Use this is you need to display numbers or for other informational purposes. */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	float RawMagnitude;

	/** Effect context, contains information about hit result, etc */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	FAbilityEffectContextHandle AbilityEffectContext;

	/** The tag name that matched this specific gameplay cue handler */
	UPROPERTY(BlueprintReadWrite, Category=GameCue, NotReplicated)
	mutable FGameplayTag MatchedTagName;

	/** The original tag of the gameplay cue */
	UPROPERTY(BlueprintReadWrite, Category=GameCue, NotReplicated)
	mutable FGameplayTag OriginalTag;

	/** The aggregated source tags taken from the effect spec */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	FGameplayTagContainer AggregatedSourceTags;

	/** The aggregated target tags taken from the effect spec */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	FGameplayTagContainer AggregatedTargetTags;

	/** Location cue took place at */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	FVector_NetQuantize10 Location;

	/** Normal of impact that caused cue */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	FVector_NetQuantizeNormal Normal;

	/** Instigator actor, the actor that owns the ability system component */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	TWeakObjectPtr<AActor> Instigator;

	/** The physical actor that actually did the damage, can be a weapon or projectile */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	TWeakObjectPtr<AActor> EffectCauser;

	/** Object this effect was created from, can be an actor or static object. Useful to bind an effect to a gameplay object */
	UPROPERTY(BlueprintReadWrite, Category=GameCue)
	TWeakObjectPtr<const UObject> SourceObject;

	/** PhysMat of the hit, if there was a hit. */
	//UPROPERTY(BlueprintReadWrite, Category = GameCue)
	//TWeakObjectPtr<const UPhysicalMaterial> PhysicalMaterial;

	/** If originating from a AbilityEffect, the level of that AbilityEffect */
	UPROPERTY(BlueprintReadWrite, Category = GameCue)
	int32 AbilityEffectLevel;

	/** If originating from an ability, this will be the level of that ability */
	UPROPERTY(BlueprintReadWrite, Category = GameCue)
	int32 AbilityLevel;

	/** Could be used to say "attach FX to this component always" */
	UPROPERTY(BlueprintReadWrite, Category = GameCue)
	TWeakObjectPtr<USceneComponent> TargetAttachComponent;

	/** If we're using a minimal replication proxy, should we replicate location for this cue */
	UPROPERTY(BlueprintReadWrite, Category = GameCue)
	bool bReplicateLocationWhenUsingMinimalRepProxy = false;

	/** If originating from a AbilityEffect, whether that AbilityEffect is still Active */
	bool bAbilityEffectActive = true;

	/** Optimized serializer */
	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);

	/** Returns true if this is locally controlled, using fallback actor if nothing else available */
	bool IsInstigatorLocallyControlled(AActor* FallbackActor = nullptr) const;

	/** Fallback actor is used if the parameters have nullptr for instigator and effect causer */
	bool IsInstigatorLocallyControlledPlayer(AActor* FallbackActor=nullptr) const;

	/** Returns the actor that instigated this originally, generally attached to an ability system component */
	AActor* GetInstigator() const;

	/** Returns the actor that physically caused the damage, could be a projectile or weapon */
	AActor* GetEffectCauser() const;

	/** Returns the object that originally caused this, game-specific but usually not an actor */
	const UObject* GetSourceObject() const;
};

template<>
struct TStructOpsTypeTraits<FGameCueParameters> : public TStructOpsTypeTraitsBase2<FGameCueParameters>
{
	enum
	{
		WithNetSerializer = true		
	};
};


USTRUCT(BlueprintType)
struct FAbilityEffectRemovalInfo
{
	GENERATED_USTRUCT_BODY()
	
	/** True when the gameplay effect's duration has not expired, meaning the gameplay effect is being forcefully removed.  */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Removal")
	bool bPrematureRemoval = false;

	/** Number of Stacks this gameplay effect had before it was removed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Removal")
	int32 StackCount = 0;

	/** Actor this gameplay effect was targeting. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Removal")
	AActor* EffectContextActor = nullptr;

	/** The Effect being Removed */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Removal")
	UGMCAbilityEffect* ActiveEffect = nullptr;
};


USTRUCT(BlueprintType)
struct  FGameplayTagRequirements
{
	GENERATED_USTRUCT_BODY()

	/** All of these tags must be present */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GameplayModifier, meta=(DisplayName="Must Have Tags"))
	FGameplayTagContainer RequireTags;

	/** None of these tags may be present */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GameplayModifier, meta=(DisplayName="Must Not Have Tags"))
	FGameplayTagContainer IgnoreTags;

	/** Build up a more complex query that can't be expressed with RequireTags/IgnoreTags alone */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = GameplayModifier, meta = (DisplayName = "Query Must Match"))
	FGameplayTagQuery TagQuery;

	/** True if all required tags and no ignore tags found */
	bool	RequirementsMet(const FGameplayTagContainer& Container) const;

	/** True if neither RequireTags or IgnoreTags has any tags */
	bool	IsEmpty() const;

	/** Return debug string */
	FString ToString() const;

	bool operator==(const FGameplayTagRequirements& Other) const;
	bool operator!=(const FGameplayTagRequirements& Other) const;


};


USTRUCT(BlueprintType)

struct FActiveAbilityEffectEvents
{
	GENERATED_USTRUCT_BODY()

	// Délégué pour notifier les changements de stack
	FOnActiveAbilityEffectStackChange OnStackChanged;
	FOnActiveAbilityEffectRemoved OnEffectRemoved;

};
