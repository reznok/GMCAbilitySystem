// Fill out your copyright notice in the Description page of Project Settings.


#include "Effects/GMCAbilityEffectTypes.h"

#include "Ability/GMCAbility.h"

#include "GameplayTagAssetInterface.h"
#include "GMCAbilityComponent.h"

class UGMCAbility;


bool FGameplayTagRequirements::RequirementsMet(const FGameplayTagContainer& Container) const
{
	const bool bHasRequired = Container.HasAll(RequireTags);
	const bool bHasIgnored = Container.HasAny(IgnoreTags);
	const bool bMatchQuery = TagQuery.IsEmpty() || TagQuery.Matches(Container);

	return bHasRequired && !bHasIgnored && bMatchQuery;
}



FGameCueParameters::FGameCueParameters(const FAbilityEffectSpecForRPC& Spec)
: NormalizedMagnitude(0.0f)
, RawMagnitude(0.0f)
, Location(ForceInitToZero)
, Normal(ForceInitToZero)
, AbilityEffectLevel(1)
, AbilityLevel(1)
{
//	UAbilitySystemGlobals::Get().InitGameCueParameters(*this, Spec);
}

FGameCueParameters::FGameCueParameters(const struct FAbilityEffectContextHandle& InAbilityEffectContext)
: NormalizedMagnitude(0.0f)
, RawMagnitude(0.0f)
, Location(ForceInitToZero)
, Normal(ForceInitToZero)
, AbilityEffectLevel(1)
, AbilityLevel(1)
{
	//UAbilitySystemGlobals::Get().InitGameCueParameters(*this, InAbilityEffectContext);
}

bool FGameCueParameters::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	static const uint8 NUM_LEVEL_BITS = 5; // need to bump this up to support 20 levels for AbilityLevel
	static const uint8 MAX_LEVEL = (1 << NUM_LEVEL_BITS) - 1;

	enum RepFlag
	{
		REP_NormalizedMagnitude = 0,
		REP_RawMagnitude,
		REP_AbilityEffectContext,
		REP_Location,
		REP_Normal,
		REP_Instigator,
		REP_EffectCauser,
		REP_SourceObject,
		REP_TargetAttachComponent,
		REP_PhysMaterial,
		REP_GELevel,
		REP_AbilityLevel,

		REP_MAX
	};

	uint16 RepBits = 0;
	if (Ar.IsSaving())
	{
		if (NormalizedMagnitude != 0.f)
		{
			RepBits |= (1 << REP_NormalizedMagnitude);
		}
		if (RawMagnitude != 0.f)
		{
			RepBits |= (1 << REP_RawMagnitude);
		}
		if (AbilityEffectContext.IsValid())
		{
			RepBits |= (1 << REP_AbilityEffectContext);
		}
		if (Location.IsNearlyZero() == false)
		{
			RepBits |= (1 << REP_Location);
		}
		if (Normal.IsNearlyZero() == false)
		{
			RepBits |= (1 << REP_Normal);
		}
		if (Instigator.IsValid())
		{
			RepBits |= (1 << REP_Instigator);
		}
		if (EffectCauser.IsValid())
		{
			RepBits |= (1 << REP_EffectCauser);
		}
		if (SourceObject.IsValid())
		{
			RepBits |= (1 << REP_SourceObject);
		}
		if (TargetAttachComponent.IsValid())
		{
			RepBits |= (1 << REP_TargetAttachComponent);
		}
		/*
		 *if (PhysicalMaterial.IsValid())
		{
			RepBits |= (1 << REP_PhysMaterial);
		}
		*/
		if (AbilityEffectLevel != 1)
		{
			RepBits |= (1 << REP_GELevel);
		}
		if (AbilityLevel != 1)
		{
			RepBits |= (1 << REP_AbilityLevel);
		}
	}

	Ar.SerializeBits(&RepBits, REP_MAX);

	// Tag containers serialize empty containers with 1 bit, so no need to serialize this in the RepBits field.
	AggregatedSourceTags.NetSerialize(Ar, Map, bOutSuccess);
	AggregatedTargetTags.NetSerialize(Ar, Map, bOutSuccess);

	if (RepBits & (1 << REP_NormalizedMagnitude))
	{
		Ar << NormalizedMagnitude;
	}
	if (RepBits & (1 << REP_RawMagnitude))
	{
		Ar << RawMagnitude;
	}
	if (RepBits & (1 << REP_AbilityEffectContext))
	{
		AbilityEffectContext.NetSerialize(Ar, Map, bOutSuccess);
	}
	if (RepBits & (1 << REP_Location))
	{
		Location.NetSerialize(Ar, Map, bOutSuccess);
	}
	if (RepBits & (1 << REP_Normal))
	{
		Normal.NetSerialize(Ar, Map, bOutSuccess);
	}
	if (RepBits & (1 << REP_Instigator))
	{
		Ar << Instigator;
	}
	if (RepBits & (1 << REP_EffectCauser))
	{
		Ar << EffectCauser;
	}
	if (RepBits & (1 << REP_SourceObject))
	{
		Ar << SourceObject;
	}
	if (RepBits & (1 << REP_TargetAttachComponent))
	{
		Ar << TargetAttachComponent;
	}
	/*if (RepBits & (1 << REP_PhysMaterial))
	{
		Ar << PhysicalMaterial;
	}*/
	if (RepBits & (1 << REP_GELevel))
	{
		ensureMsgf(AbilityEffectLevel <= MAX_LEVEL, TEXT("FGameCueParameters::NetSerialize trying to serialize GC parameters with a AbilityEffectLevel of %d"), AbilityEffectLevel);
		if (Ar.IsLoading())
		{
			AbilityEffectLevel = 0;
		}

		Ar.SerializeBits(&AbilityEffectLevel, NUM_LEVEL_BITS);
	}
	if (RepBits & (1 << REP_AbilityLevel))
	{
		ensureMsgf(AbilityLevel <= MAX_LEVEL, TEXT("FGameCueParameters::NetSerialize trying to serialize GC parameters with an AbilityLevel of %d"), AbilityLevel);
		if (Ar.IsLoading())
		{
			AbilityLevel = 0;
		}

		Ar.SerializeBits(&AbilityLevel, NUM_LEVEL_BITS);
	}

	bOutSuccess = true;
	return true;
}

FString EGameCueEventToString(int32 Type)
{
	static UEnum *e = StaticEnum<EGameCueEvent::Type>();
	return e->GetNameStringByValue(Type);
}


bool FGameCueParameters::IsInstigatorLocallyControlled(AActor* FallbackActor) const
{
	if (AbilityEffectContext.IsValid())
	{
		return AbilityEffectContext.IsLocallyControlled();
	}

	APawn* Pawn = Cast<APawn>(Instigator.Get());
	if (!Pawn)
	{
		Pawn = Cast<APawn>(EffectCauser.Get());
		if (!Pawn && FallbackActor != nullptr)
		{
			// Fallback to passed in actor
			Pawn = Cast<APawn>(FallbackActor);
			if (!Pawn)
			{
				Pawn = FallbackActor->GetInstigator<APawn>();
			}
		}
	}
	if (Pawn)
	{
		return Pawn->IsLocallyControlled();
	}
	return false;
}

bool FGameCueParameters::IsInstigatorLocallyControlledPlayer(AActor* FallbackActor) const
{
	// If there is an effect context, just ask it
	if (AbilityEffectContext.IsValid())
	{
		return AbilityEffectContext.IsLocallyControlledPlayer();
	}
	
	// Look for a pawn and use its controller
	{
		APawn* Pawn = Cast<APawn>(Instigator.Get());
		if (!Pawn)
		{
			// If no instigator, look at effect causer
			Pawn = Cast<APawn>(EffectCauser.Get());
			if (!Pawn && FallbackActor != nullptr)
			{
				// Fallback to passed in actor
				Pawn = Cast<APawn>(FallbackActor);
				if (!Pawn)
				{
					Pawn = FallbackActor->GetInstigator<APawn>();
				}
			}
		}

		if (Pawn && Pawn->Controller)
		{
			return Pawn->Controller->IsLocalPlayerController();
		}
	}

	return false;
}

AActor* FGameCueParameters::GetInstigator() const
{
	if (Instigator.IsValid())
	{
		return Instigator.Get();
	}

	// Fallback to effect context if the explicit data on GameCue parameters is not there.
	return AbilityEffectContext.GetInstigator();
}

AActor* FGameCueParameters::GetEffectCauser() const
{
	if (EffectCauser.IsValid())
	{
		return EffectCauser.Get();
	}

	// Fallback to effect context if the explicit data on GameCue parameters is not there.
	return AbilityEffectContext.GetEffectCauser();
}

const UObject* FGameCueParameters::GetSourceObject() const
{
	if (SourceObject.IsValid())
	{
		return SourceObject.Get();
	}

	// Fallback to effect context if the explicit data on GameCue parameters is not there.
	return AbilityEffectContext.GetSourceObject();
}




FOnAbilityEffectTagCountChanged& FGameplayTagCountContainer::RegisterGameplayTagEvent(const FGameplayTag& Tag, EGameplayTagEventType::Type EventType)
{
	FDelegateInfo& Info = GameplayTagEventMap.FindOrAdd(Tag);

	if (EventType == EGameplayTagEventType::NewOrRemoved)
	{
		return Info.OnNewOrRemove;
	}

	return Info.OnAnyChange;
}

void FGameplayTagCountContainer::Reset()
{
	GameplayTagEventMap.Reset();
	GameplayTagCountMap.Reset();
	ExplicitTagCountMap.Reset();
	ExplicitTags.Reset();
	OnAnyTagChangeDelegate.Clear();
}

bool FGameplayTagCountContainer::UpdateExplicitTags(const FGameplayTag& Tag, const int32 CountDelta, const bool bDeferParentTagsOnRemove)
{
	const bool bTagAlreadyExplicitlyExists = ExplicitTags.HasTagExact(Tag);

	// Need special case handling to maintain the explicit tag list correctly, adding the tag to the list if it didn't previously exist and a
	// positive delta comes in, and removing it from the list if it did exist and a negative delta comes in.
	if (!bTagAlreadyExplicitlyExists)
	{
		// Brand new tag with a positive delta needs to be explicitly added
		if (CountDelta > 0)
		{
			ExplicitTags.AddTag(Tag);
		}
		// Block attempted reduction of non-explicit tags, as they were never truly added to the container directly
		else
		{
			// only warn about tags that are in the container but will not be removed because they aren't explicitly in the container
			if (ExplicitTags.HasTag(Tag))
			{
			//	UE_LOG(Warning, TEXT("Attempted to remove tag: %s from tag count container, but it is not explicitly in the container!"), *Tag.ToString());
			}
			return false;
		}
	}

	// Update the explicit tag count map. This has to be separate than the map below because otherwise the count of nested tags ends up wrong
	int32& ExistingCount = ExplicitTagCountMap.FindOrAdd(Tag);

	ExistingCount = FMath::Max(ExistingCount + CountDelta, 0);

	// If our new count is 0, remove us from the explicit tag list
	if (ExistingCount <= 0)
	{
		// Remove from the explicit list
		ExplicitTags.RemoveTag(Tag, bDeferParentTagsOnRemove);
	}

	return true;
}

bool FGameplayTagCountContainer::GatherTagChangeDelegates(const FGameplayTag& Tag, const int32 CountDelta, TArray<FDeferredTagChangeDelegate>& TagChangeDelegates)
{
	// Check if change delegates are required to fire for the tag or any of its parents based on the count change
	FGameplayTagContainer TagAndParentsContainer = Tag.GetGameplayTagParents();
	bool CreatedSignificantChange = false;
	for (auto CompleteTagIt = TagAndParentsContainer.CreateConstIterator(); CompleteTagIt; ++CompleteTagIt)
	{
		const FGameplayTag& CurTag = *CompleteTagIt;

		// Get the current count of the specified tag. NOTE: Stored as a reference, so subsequent changes propagate to the map.
		int32& TagCountRef = GameplayTagCountMap.FindOrAdd(CurTag);

		const int32 OldCount = TagCountRef;

		// Apply the delta to the count in the map
		int32 NewTagCount = FMath::Max(OldCount + CountDelta, 0);
		TagCountRef = NewTagCount;

		// If a significant change (new addition or total removal) occurred, trigger related delegates
		const bool SignificantChange = (OldCount == 0 || NewTagCount == 0);
		CreatedSignificantChange |= SignificantChange;
		if (SignificantChange)
		{
			TagChangeDelegates.AddDefaulted();
			TagChangeDelegates.Last().BindLambda([Delegate = OnAnyTagChangeDelegate, CurTag, NewTagCount]()
			{
				Delegate.Broadcast(CurTag, NewTagCount);
			});
		}

		FDelegateInfo* DelegateInfo = GameplayTagEventMap.Find(CurTag);
		if (DelegateInfo)
		{
			TagChangeDelegates.AddDefaulted();
			TagChangeDelegates.Last().BindLambda([Delegate = DelegateInfo->OnAnyChange, CurTag, NewTagCount]()
			{
				Delegate.Broadcast(CurTag, NewTagCount);
			});

			if (SignificantChange)
			{
				TagChangeDelegates.AddDefaulted();
				TagChangeDelegates.Last().BindLambda([Delegate = DelegateInfo->OnNewOrRemove, CurTag, NewTagCount]()
				{
					Delegate.Broadcast(CurTag, NewTagCount);
				});
			}
		}
	}

	return CreatedSignificantChange;
}

bool FGameplayTagCountContainer::UpdateTagMap_Internal(const FGameplayTag& Tag, int32 CountDelta)
{
	if (!UpdateExplicitTags(Tag, CountDelta, false))
	{
		return false;
	}

	TArray<FDeferredTagChangeDelegate> DeferredTagChangeDelegates;
	bool bSignificantChange = GatherTagChangeDelegates(Tag, CountDelta, DeferredTagChangeDelegates);
	for (FDeferredTagChangeDelegate& Delegate : DeferredTagChangeDelegates)
	{
		Delegate.Execute();
	}

	return bSignificantChange;
}

bool FGameplayTagCountContainer::UpdateTagMapDeferredParentRemoval_Internal(const FGameplayTag& Tag, int32 CountDelta, TArray<FDeferredTagChangeDelegate>& DeferredTagChangeDelegates)
{
	if (!UpdateExplicitTags(Tag, CountDelta, true))
	{
		return false;
	}

	return GatherTagChangeDelegates(Tag, CountDelta, DeferredTagChangeDelegates);
}

// --------------------------------------------------------------------------------------------------------------------------------------------------------
//
//	FAbilityEffectContext
//
// --------------------------------------------------------------------------------------------------------------------------------------------------------

bool FAbilityEffectContext::CanActorReferenceBeReplicated(const AActor* Actor)
{
	// We always support replication of null references and stably named actors
	if (!Actor || Actor->IsFullNameStableForNetworking())
	{
		return true;
	}

	// If we get here this is a dynamic object and we only want to replicate the reference if the actor is set to replicate, otherwise the resolve on the client will constantly fail
	const bool bIsSupportedForNetWorking = Actor->IsSupportedForNetworking();
	const bool bCanDynamicReferenceBeReplicated = bIsSupportedForNetWorking && Actor->GetIsReplicated();

#if !UE_BUILD_SHIPPING
	// Optionally trigger warning if we are trying to replicate a reference to an object that never will be resolvable on receiving end
	/*if (UE::Private::bWarnIfTryingToReplicateNotSupportedActorReference && (!bCanDynamicReferenceBeReplicated && bIsSupportedForNetWorking))
	{
		//UE_LOG(Warning, TEXT("Attempted to replicate a reference to dynamically spawned object that is set to not replicate %s."), *(Actor->GetName()));
	}*/
#endif

	return bCanDynamicReferenceBeReplicated;
}

void FAbilityEffectContext::AddInstigator(class AActor *InInstigator, class AActor *InEffectCauser)
{
	Instigator = InInstigator;
	bReplicateInstigator = CanActorReferenceBeReplicated(InInstigator);

	SetEffectCauser(InEffectCauser);

	InstigatorAbilitySystemComponent = NULL;

	// Cache off the AbilitySystemComponent.
	InstigatorAbilitySystemComponent =	 Cast<UGMC_AbilitySystemComponent>(Instigator->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
}

void FAbilityEffectContext::SetAbility(const UGMCAbility* InGameplayAbility)
{
	if (InGameplayAbility)
	{
		AbilityInstanceNotReplicated = MakeWeakObjectPtr(const_cast<UGMCAbility*>(InGameplayAbility));
		AbilityCDO = InGameplayAbility->GetClass()->GetDefaultObject<UGMCAbility>();
		AbilityLevel = InGameplayAbility->GetAbilityLevel();
	}
}

const UGMCAbility* FAbilityEffectContext::GetAbility() const
{
	return AbilityCDO.Get();
}

const UGMCAbility* FAbilityEffectContext::GetAbilityInstance_NotReplicated() const
{
	return AbilityInstanceNotReplicated.Get();
}


void FAbilityEffectContext::AddActors(const TArray<TWeakObjectPtr<AActor>>& InActors, bool bReset)
{
	if (bReset && Actors.Num())
	{
		Actors.Reset();
	}

	Actors.Append(InActors);
}

void FAbilityEffectContext::AddHitResult(const FHitResult& InHitResult, bool bReset)
{
	if (bReset && HitResult.IsValid())
	{
		HitResult.Reset();
		bHasWorldOrigin = false;
	}

	check(!HitResult.IsValid());
	HitResult = TSharedPtr<FHitResult>(new FHitResult(InHitResult));
	if (bHasWorldOrigin == false)
	{
		AddOrigin(InHitResult.TraceStart);
	}
}

bool FAbilityEffectContext::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	uint8 RepBits = 0;
	if (Ar.IsSaving())
	{
		if (bReplicateInstigator && Instigator.IsValid())
		{
			RepBits |= 1 << 0;
		}
		if (bReplicateEffectCauser && EffectCauser.IsValid() )
		{
			RepBits |= 1 << 1;
		}
		if (AbilityCDO.IsValid())
		{
			RepBits |= 1 << 2;
		}
		if (bReplicateSourceObject && SourceObject.IsValid())
		{
			RepBits |= 1 << 3;
		}
		if (Actors.Num() > 0)
		{
			RepBits |= 1 << 4;
		}
		if (HitResult.IsValid())
		{
			RepBits |= 1 << 5;
		}
		if (bHasWorldOrigin)
		{
			RepBits |= 1 << 6;
		}
	}

	Ar.SerializeBits(&RepBits, 7);

	if (RepBits & (1 << 0))
	{
		Ar << Instigator;
	}
	if (RepBits & (1 << 1))
	{
		Ar << EffectCauser;
	}
	if (RepBits & (1 << 2))
	{
		Ar << AbilityCDO;
	}
	if (RepBits & (1 << 3))
	{
		Ar << SourceObject;
	}
	if (RepBits & (1 << 4))
	{
		SafeNetSerializeTArray_Default<31>(Ar, Actors);
	}
	if (RepBits & (1 << 5))
	{
		if (Ar.IsLoading())
		{
			if (!HitResult.IsValid())
			{
				HitResult = TSharedPtr<FHitResult>(new FHitResult());
			}
		}
		HitResult->NetSerialize(Ar, Map, bOutSuccess);
	}
	if (RepBits & (1 << 6))
	{
		Ar << WorldOrigin;
		bHasWorldOrigin = true;
	}
	else
	{
		bHasWorldOrigin = false;
	}

	if (Ar.IsLoading())
	{
		AddInstigator(Instigator.Get(), EffectCauser.Get()); // Just to initialize InstigatorAbilitySystemComponent
	}	
	
	bOutSuccess = true;
	return true;
}

FString FAbilityEffectContext::ToString() const
{
	const AActor* InstigatorPtr = Instigator.Get();
	return (InstigatorPtr ? InstigatorPtr->GetName() : FString(TEXT("NONE")));
}

bool FAbilityEffectContext::IsLocallyControlled() const
{
	APawn* Pawn = Cast<APawn>(Instigator.Get());
	if (!Pawn)
	{
		Pawn = Cast<APawn>(EffectCauser.Get());
	}
	if (Pawn)
	{
		return Pawn->IsLocallyControlled();
	}
	return false;
}

bool FAbilityEffectContext::IsLocallyControlledPlayer() const
{
	APawn* Pawn = Cast<APawn>(Instigator.Get());
	if (!Pawn)
	{
		Pawn = Cast<APawn>(EffectCauser.Get());
	}
	if (Pawn && Pawn->Controller)
	{
		return Pawn->Controller->IsLocalPlayerController();
	}
	return false;
}

void FAbilityEffectContext::AddOrigin(FVector InOrigin)
{
	bHasWorldOrigin = true;
	WorldOrigin = InOrigin;
}

void FAbilityEffectContext::GetOwnedGameplayTags(OUT FGameplayTagContainer& ActorTagContainer, OUT FGameplayTagContainer& SpecTagContainer) const
{
	IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(Instigator.Get());
	if (TagInterface)
	{
		TagInterface->GetOwnedGameplayTags(ActorTagContainer);
	}
	else if (UGMC_AbilitySystemComponent* ASC = InstigatorAbilitySystemComponent.Get())
	{
		ASC->GetOwnedGameplayTags(ActorTagContainer);
	}
}

struct FAbilityEffectContextDeleter
{
	FORCEINLINE void operator()(FAbilityEffectContext* Object) const
	{
		check(Object);
		UScriptStruct* ScriptStruct = Object->GetScriptStruct();
		check(ScriptStruct);
		ScriptStruct->DestroyStruct(Object);
		FMemory::Free(Object);
	}
};

bool FAbilityEffectContextHandle::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	bool ValidData = Data.IsValid();
	Ar.SerializeBits(&ValidData,1);

	if (ValidData)
	{
		TCheckedObjPtr<UScriptStruct> ScriptStruct = Data.IsValid() ? Data->GetScriptStruct() : nullptr;
		
		//UAbilitySystemGlobals::Get().EffectContextStructCache.NetSerialize(Ar, ScriptStruct.Get());

		if (ScriptStruct.IsValid())
		{
			if (Ar.IsLoading())
			{
				// If data is invalid, or a different type, allocate
				if (!Data.IsValid() || (Data->GetScriptStruct() != ScriptStruct.Get()))
				{
					FAbilityEffectContext* NewData = (FAbilityEffectContext*)FMemory::Malloc(ScriptStruct->GetStructureSize());
					ScriptStruct->InitializeStruct(NewData);

					Data = TSharedPtr<FAbilityEffectContext>(NewData, FAbilityEffectContextDeleter());
				}
			}

			check(Data.IsValid());
			if (ScriptStruct->StructFlags & STRUCT_NetSerializeNative)
			{
				ScriptStruct->GetCppStructOps()->NetSerialize(Ar, Map, bOutSuccess, Data.Get());
			}
			else
			{
				// This won't work since FStructProperty::NetSerializeItem is deprecrated.
				//	1) we have to manually crawl through the topmost struct's fields since we don't have a FStructProperty for it (just the UScriptProperty)
				//	2) if there are any UStructProperties in the topmost struct's fields, we will assert in FStructProperty::NetSerializeItem.

				//ABILITY_LOG(Fatal, TEXT("FAbilityEffectContextHandle::NetSerialize called on data struct %s without a native NetSerialize"), *ScriptStruct->GetName());
			}
		}
		else if (ScriptStruct.IsError())
		{
			//ABILITY_LOG(Error, TEXT("FAbilityEffectContextHandle::NetSerialize: Bad ScriptStruct serialized, can't recover."));
			Ar.SetError();
			Data.Reset();
			bOutSuccess = false;
			return false;
		}
	}
	else
	{
		Data.Reset();
	}

	bOutSuccess = true;
	return true;
}
