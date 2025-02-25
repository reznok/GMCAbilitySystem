// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCues/GMC_GameCueInterface.h"

#include "GameplayTagsModule.h"
#include "GMCAbilityComponent.h"
#include "GameCues/GMC_GameCueSet.h"
#include "Engine/PackageMapClient.h"

//#include UE_INLINE_GENERATED_CPP_BY_NAME(GameCueInterface)


namespace GameCueInterfacePrivate
{
	struct FCueNameAndUFunction
	{
		FGameplayTag Tag;
		UFunction* Func;
	};
	typedef TMap<FGameplayTag, TArray<FCueNameAndUFunction> > FGameCueTagFunctionList;
	static TMap<FObjectKey, FGameCueTagFunctionList > PerClassGameplayTagToFunctionMap;
}


UGameCueInterface::UGameCueInterface(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

void IGameCueInterface::DispatchBlueprintCustomHandler(UObject* Object, UFunction* Func, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	// Vous pouvez essayer d'éviter d'utiliser le type généré
	struct FCustomHandlerParams
	{
		EGameCueEvent::Type EventType;
		FGameCueParameters Parameters;
	};

	FCustomHandlerParams Params;
	Params.EventType = EventType;
	Params.Parameters = Parameters;

	// Appel direct avec les bons paramètres
	Object->ProcessEvent(Func, &Params);
}
void IGameCueInterface::ClearTagToFunctionMap()
{
	GameCueInterfacePrivate::PerClassGameplayTagToFunctionMap.Empty();
}

void IGameCueInterface::HandleGameCues(AActor *Self, const FGameplayTagContainer& GameCueTags, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	HandleGameCues((UObject*)Self, GameCueTags, EventType, Parameters);
}

void IGameCueInterface::HandleGameCues(UObject* Self, const FGameplayTagContainer& GameCueTags, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	for (FGameplayTag CueTag : GameCueTags)
	{
		HandleGameCue(Self, CueTag, EventType, Parameters);
	}
}

bool IGameCueInterface::ShouldAcceptGameCue(AActor *Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	return ShouldAcceptGameCue((UObject*)Self, GameCueTag, EventType, Parameters);
}

bool IGameCueInterface::ShouldAcceptGameCue(UObject* Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	return true;
}

void IGameCueInterface::HandleGameCue(AActor *Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	HandleGameCue((UObject*)Self, GameCueTag, EventType, Parameters);
}

void IGameCueInterface::HandleGameCue(UObject* Self, FGameplayTag GameCueTag, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
//	SCOPE_CYCLE_COUNTER(STAT_GameCueInterface_HandleGameCue);

	if (!Self)
	{
		return;
	}

	// Look up a custom function for this gameplay tag. 
	UClass* Class = Self->GetClass();
	FGameplayTagContainer TagAndParentsContainer = GameCueTag.GetGameplayTagParents();

	Parameters.OriginalTag = GameCueTag;

	//Find entry for the class
	FObjectKey ClassObjectKey(Class);
	GameCueInterfacePrivate::FGameCueTagFunctionList& GameplayTagFunctionList = GameCueInterfacePrivate::PerClassGameplayTagToFunctionMap.FindOrAdd(ClassObjectKey);
	TArray<GameCueInterfacePrivate::FCueNameAndUFunction>* FunctionList = GameplayTagFunctionList.Find(GameCueTag);
	if (FunctionList == NULL)
	{
		//generate new function list
		FunctionList = &GameplayTagFunctionList.Add(GameCueTag);

		for (auto InnerTagIt = TagAndParentsContainer.CreateConstIterator(); InnerTagIt; ++InnerTagIt)
		{
			UFunction* Func = NULL;
			FName CueName = InnerTagIt->GetTagName();

			Func = Class->FindFunctionByName(CueName, EIncludeSuperFlag::IncludeSuper);
			// If the handler calls ForwardGameCueToParent, keep calling functions until one consumes the cue and doesn't forward it
			while (Func)
			{
				GameCueInterfacePrivate::FCueNameAndUFunction NewCueFunctionPair;
				NewCueFunctionPair.Tag = *InnerTagIt;
				NewCueFunctionPair.Func = Func;
				FunctionList->Add(NewCueFunctionPair);

				Func = Func->GetSuperFunction();
			}

			// Native functions cant be named with ".", so look for them with _. 
			FName NativeCueFuncName = *CueName.ToString().Replace(TEXT("."), TEXT("_"));
			Func = Class->FindFunctionByName(NativeCueFuncName, EIncludeSuperFlag::IncludeSuper);

			while (Func)
			{
				GameCueInterfacePrivate::FCueNameAndUFunction NewCueFunctionPair;
				NewCueFunctionPair.Tag = *InnerTagIt;
				NewCueFunctionPair.Func = Func;
				FunctionList->Add(NewCueFunctionPair);

				Func = Func->GetSuperFunction();
			}
		}
	}

	//Iterate through all functions in the list until we should no longer continue
	check(FunctionList);
		
	bool bShouldContinue = true;
	for (int32 FunctionIndex = 0; bShouldContinue && (FunctionIndex < FunctionList->Num()); ++FunctionIndex)
	{
		const GameCueInterfacePrivate::FCueNameAndUFunction& CueFunctionPair = (*FunctionList)[FunctionIndex];
		UFunction* Func = CueFunctionPair.Func;
		Parameters.MatchedTagName = CueFunctionPair.Tag;

		// Reset the forward parameter now, so we can check it after function
		bForwardToParent = false;
		IGameCueInterface::DispatchBlueprintCustomHandler(Self, Func, EventType, Parameters);

		bShouldContinue = bForwardToParent;
	}

	if (bShouldContinue)
	{
		if (AActor* SelfActor = Cast<AActor>(Self))
		{
			TArray<UGameCueSet*> Sets;
			GetGameCueSets(Sets);
			for (UGameCueSet* Set : Sets)
			{
				bShouldContinue = Set->HandleGameCue(SelfActor, GameCueTag, EventType, Parameters);
				if (!bShouldContinue)
				{
					break;
				}
			}
		}
	}

	if (bShouldContinue)
	{
		Parameters.MatchedTagName = GameCueTag;
		GameCueDefaultHandler(EventType, Parameters);
	}
}

void IGameCueInterface::GameCueDefaultHandler(EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	// No default handler, subclasses can implement
}

void IGameCueInterface::ForwardGameCueToParent()
{
	// Consumed by HandleGameCue
	bForwardToParent = true;
}

void FActiveGameCue::PreReplicatedRemove(const struct FActiveGameCueContainer &InArray)
{
	if (!InArray.Owner)
	{
		return;
	}

	// We don't check the PredictionKey here like we do in PostReplicatedAdd. PredictionKey tells us
	// if we were predictely created, but this doesn't mean we will predictively remove ourselves.
	if (bPredictivelyRemoved == false)
	{
		// If predicted ignore the add/remove
		InArray.Owner->UpdateTagMap(GameCueTag, -1);
		InArray.Owner->InvokeGameCueEvent(GameCueTag, EGameCueEvent::Removed, Parameters);
	}
}

void FActiveGameCue::PostReplicatedAdd(const struct FActiveGameCueContainer &InArray)
{
	if (!InArray.Owner)
	{
		return;
	}

	InArray.Owner->UpdateTagMap(GameCueTag, 1);

	if (PredictionKey.IsLocalClientKey() == false)
	{
		// If predicted ignore the add/remove
		InArray.Owner->InvokeGameCueEvent(GameCueTag, EGameCueEvent::WhileActive, Parameters);
	}
}

FString FActiveGameCue::GetDebugString()
{
	return FString::Printf(TEXT("(%s / %s"), *GameCueTag.ToString(), *PredictionKey.ToString());
}

void FActiveGameCueContainer::AddCue(const FGameplayTag& Tag, const FPredictionKey& PredictionKey, const FGameCueParameters& Parameters)
{
	if (!Owner)
	{
		return;
	}

	UWorld* World = Owner->GetWorld();

	// Store the prediction key so the client can investigate it
	FActiveGameCue&	NewCue = GameCues[GameCues.AddDefaulted()];
	NewCue.GameCueTag = Tag;
	NewCue.PredictionKey = PredictionKey;
	NewCue.Parameters = Parameters;
	MarkItemDirty(NewCue);
	
	Owner->UpdateTagMap(Tag, 1);
}

void FActiveGameCueContainer::RemoveCue(const FGameplayTag& Tag)
{
	if (!Owner)
	{
		return;
	}

	
	// Iterate backwards so we can remove during loop
	int32 CountDelta = 0;
	for (int32 idx=GameCues.Num()-1; idx >= 0; --idx)
	{
		FActiveGameCue& Cue = GameCues[idx];

		if (Cue.GameCueTag == Tag)
		{
			GameCues.RemoveAt(idx);
			CountDelta -= 1;
		}
	}

	if (CountDelta < 0)
	{
		MarkArrayDirty();
		Owner->UpdateTagMap(Tag, CountDelta);
	}
}

void FActiveGameCueContainer::RemoveAllCues()
{
	if (!Owner)
	{
		return;
	}

	for (int32 idx=0; idx < GameCues.Num(); ++idx)
	{
		FActiveGameCue& Cue = GameCues[idx];
		Owner->UpdateTagMap(Cue.GameCueTag, -1);
		Owner->InvokeGameCueEvent(Cue.GameCueTag, EGameCueEvent::Removed, Cue.Parameters);
	}
}

void FActiveGameCueContainer::PredictiveRemove(const FGameplayTag& Tag)
{
	if (!Owner)
	{
		return;
	}
	

	// Predictive remove: we are predicting the removal of a replicated cue
	// (We are not predicting the removal of a predictive cue. The predictive cue will be implicitly removed when the prediction key catched up)
	for (int32 idx=0; idx < GameCues.Num(); ++idx)
	{
		// "Which" cue we predictively remove is only based on the tag and not already being predictively removed.
		// Since there are no handles/identifies for the items in this container, we just go with the first.
		FActiveGameCue& Cue = GameCues[idx];
		if (Cue.GameCueTag == Tag && !Cue.bPredictivelyRemoved)
		{
			Cue.bPredictivelyRemoved = true;
			Owner->UpdateTagMap(Tag, -1);
			Owner->InvokeGameCueEvent(Tag, EGameCueEvent::Removed, Cue.Parameters);	
			return;
		}
	}
}

void FActiveGameCueContainer::PredictiveAdd(const FGameplayTag& Tag, FPredictionKey& PredictionKey)
{
	if (!Owner)
	{
		return;
	}

	Owner->UpdateTagMap(Tag, 1);	
	PredictionKey.NewRejectOrCaughtUpDelegate(FPredictionKeyEvent::CreateUObject(ToRawPtr(Owner), &UGMC_AbilitySystemComponent::OnPredictiveGameCueCatchup, Tag));
}

bool FActiveGameCueContainer::HasCue(const FGameplayTag& Tag) const
{
	for (int32 idx=0; idx < GameCues.Num(); ++idx)
	{
		const FActiveGameCue& Cue = GameCues[idx];
		if (Cue.GameCueTag == Tag)
		{
			return true;
		}
	}

	return false;
}

bool FActiveGameCueContainer::ShouldReplicate() const
{/*
	if (bMinimalReplication && (Owner && Owner->ReplicationMode == EAbilityEffectReplicationMode::Full))
	{
		return false;
	}
*/
	return true;
}

bool FActiveGameCueContainer::NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms)
{
	if (!ShouldReplicate())
	{
		return false;
	}

	return FastArrayDeltaSerialize<FActiveGameCue>(GameCues, DeltaParms, *this);
}

void FActiveGameCueContainer::SetOwner(UGMC_AbilitySystemComponent* InOwner)
{
	Owner = InOwner;
	
	// If we already have cues, pretend they were just added
	for (FActiveGameCue& Cue : GameCues)
	{
		Cue.PostReplicatedAdd(*this);
	}
}

// ----------------------------------------------------------------------------------------

FMinimalGameCueReplicationProxy::FMinimalGameCueReplicationProxy()
{
	InitGameCueParametersFunc = [](FGameCueParameters& GameCueParameters, UGMC_AbilitySystemComponent* InOwner)
	{
		if (InOwner)
		{
			InOwner->InitDefaultGameCueParameters(GameCueParameters);
		}
	};
}

void FMinimalGameCueReplicationProxy::SetOwner(UGMC_AbilitySystemComponent* ASC)
{
	Owner = ASC;
	if (Owner && ReplicatedTags.Num() > 0)
	{
		// Invoke events in case we skipped them during ::NetSerialize
		FGameCueParameters Parameters;
		InitGameCueParametersFunc(Parameters, Owner);

		for (FGameplayTag& Tag : ReplicatedTags)
		{
			Owner->SetTagMapCount(Tag, 1);
			Owner->InvokeGameCueEvent(Tag, EGameCueEvent::WhileActive, Parameters);
		}
	}
}

void FMinimalGameCueReplicationProxy::PreReplication(const FActiveGameCueContainer& SourceContainer)
{
	if (LastSourceArrayReplicationKey != SourceContainer.ArrayReplicationKey)
	{
		LastSourceArrayReplicationKey = SourceContainer.ArrayReplicationKey;
		ReplicatedTags.SetNum(SourceContainer.GameCues.Num(), false);
		ReplicatedLocations.SetNum(SourceContainer.GameCues.Num(), false);
		for (int32 idx=0; idx < SourceContainer.GameCues.Num(); ++idx)
		{
			ReplicatedTags[idx] = SourceContainer.GameCues[idx].GameCueTag;
			if (SourceContainer.GameCues[idx].Parameters.bReplicateLocationWhenUsingMinimalRepProxy)
			{
				ReplicatedLocations[idx] = SourceContainer.GameCues[idx].Parameters.Location;
			}
			else
			{
				ReplicatedLocations[idx] = FVector::ZeroVector;
			}
		}
	}
}

bool FMinimalGameCueReplicationProxy::NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess)
{
	enum { NumBits = 5 }; // Number of bits to use for number of array
	enum { MaxNum = (1 << NumBits) -1 }; // Number of bits to use for number of array

	uint8 NumElements;

	if (Ar.IsSaving())
	{
		NumElements = ReplicatedTags.Num();
		if (NumElements > MaxNum)
		{
			FString Str;
			for (const FGameplayTag& Tag : ReplicatedTags)
			{
				Str += Tag.ToString() + TEXT(" ");
			}
			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Too many tags in ReplicatedTags on %s. %d total: %s. Dropping"), *GetPathNameSafe(Owner), NumElements, *Str);
			NumElements = MaxNum;
			ReplicatedTags.SetNum(NumElements);
		}

		Ar.SerializeBits(&NumElements, NumBits);

		for (uint8 i=0; i < NumElements; ++i)
		{
			ReplicatedTags[i].NetSerialize(Ar, Map, bOutSuccess);
			if (ReplicatedLocations[i].IsZero())
			{
				bool bHasLocation = false;
				Ar << bHasLocation;
			}
			else
			{
				bool bHasLocation = true;
				Ar << bHasLocation;
				ReplicatedLocations[i].NetSerialize(Ar, Map, bOutSuccess);
			}
		}
	}
	else
	{
		// Only actually update the owner's tag map if we not the 
		bool UpdateOwnerTagMap = Owner != nullptr;
		if (bRequireNonOwningNetConnection && Owner)
		{
			if (AActor* OwningActor = Owner->GetOwner())
			{
				// Note we deliberately only want to do this if the NetConnection is not null
				if (UNetConnection* OwnerNetConnection = OwningActor->GetNetConnection()) 
				{
					if (OwnerNetConnection == CastChecked<UPackageMapClient>(Map)->GetConnection())
					{
						UpdateOwnerTagMap = false;
					}
				}
			}
		}


		NumElements = 0;
		Ar.SerializeBits(&NumElements, NumBits);

		LocalTags = MoveTemp(ReplicatedTags);
		LocalBitMask.Init(true, LocalTags.Num());
		
		ReplicatedTags.SetNumUninitialized(NumElements, false);
		ReplicatedLocations.SetNum(NumElements, false);

		// This struct does not serialize GC parameters but will synthesize them on the receiving side.
		FGameCueParameters Parameters;
		InitGameCueParametersFunc(Parameters, Owner);
		FVector OriginalLocationParameter = Parameters.Location;

		for (uint8 i=0; i < NumElements; ++i)
		{
			FGameplayTag& ReplicatedTag = ReplicatedTags[i];

			ReplicatedTag.NetSerialize(Ar, Map, bOutSuccess);

			bool bHasReplicatedLocation = false;
			Ar << bHasReplicatedLocation;
			if (bHasReplicatedLocation)
			{
				FVector_NetQuantize& ReplicatedLocation = ReplicatedLocations[i];
				ReplicatedLocation.NetSerialize(Ar, Map, bOutSuccess);
				Parameters.Location = ReplicatedLocation;
			}
			else
			{
				Parameters.Location = OriginalLocationParameter;
			}

			int32 LocalIdx = LocalTags.IndexOfByKey(ReplicatedTag);
			if (LocalIdx != INDEX_NONE)
			{
				// This tag already existed and is accounted for
				LocalBitMask[LocalIdx] = false;
			}
			else if (UpdateOwnerTagMap)
			{
				bCachedModifiedOwnerTags = true;
				// This is a new tag, we need to invoke the WhileActive GameCue event
				Owner->SetTagMapCount(ReplicatedTag, 1);
				Owner->InvokeGameCueEvent(ReplicatedTag, EGameCueEvent::WhileActive, Parameters);

				// The demo recorder needs to believe that this structure is dirty so it will get saved into the demo stream
				LastSourceArrayReplicationKey++;
			}
		}

		// Restore the location in case we touched it
		Parameters.Location = OriginalLocationParameter;

		if (UpdateOwnerTagMap)
		{
			bCachedModifiedOwnerTags = true;
			for (TConstSetBitIterator<TInlineAllocator<NumInlineTags>> It(LocalBitMask); It; ++It)
			{
				FGameplayTag& RemovedTag = LocalTags[It.GetIndex()];
				Owner->SetTagMapCount(RemovedTag, 0);
				Owner->InvokeGameCueEvent(RemovedTag, EGameCueEvent::Removed, Parameters);

				// The demo recorder needs to believe that this structure is dirty so it will get saved into the demo stream
				LastSourceArrayReplicationKey++;
			}
		}
	}


	bOutSuccess = true;
	return true;
}

void FMinimalGameCueReplicationProxy::RemoveAllCues()
{
	if (!Owner || !bCachedModifiedOwnerTags)
	{
		return;
	}

	FGameCueParameters Parameters;
	InitGameCueParametersFunc(Parameters, Owner);

	for (int32 idx=0; idx < ReplicatedTags.Num(); ++idx)
	{
		const FGameplayTag& GameCueTag = ReplicatedTags[idx];
		Owner->SetTagMapCount(GameCueTag, 0);
		Owner->InvokeGameCueEvent(GameCueTag, EGameCueEvent::Removed, Parameters);
	}
}
