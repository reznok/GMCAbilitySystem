// Copyright Epic Games, Inc. All Rights Reserved.
#include "GameCues/GameCueNotify_Actor.h"
#include "TimerManager.h"
#include "Engine/Blueprint.h"
#include "Components/TimelineComponent.h"
//#include "AbilitySystemStats.h"


#include "GMCAbilitySystemGlobals.h"
#include "GameCues/GMC_GameCueManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameCueNotify_Actor)


namespace FAbilitySystemTweaks
{
	int ClearCueNotifyTimers = 1;
	FAutoConsoleVariableRef CVarClearCueNotifyTimers(TEXT("AbilitySystem.ClearCueNotifyTimers"), FAbilitySystemTweaks::ClearCueNotifyTimers, TEXT("Whether to call ClearAllTimersForObject when cue is getting recycled"), ECVF_Default);
}


AGameCueNotify_Actor::AGameCueNotify_Actor(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
	IsOverride = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	bAutoDestroyOnRemove = false;
	AutoDestroyDelay = 0.f;
	bUniqueInstancePerSourceObject = false;
	bUniqueInstancePerInstigator = false;
	bAllowMultipleOnActiveEvents = true;
	bAllowMultipleWhileActiveEvents = true;
	bHasHandledOnRemoveEvent = false;

	NumPreallocatedInstances = 0;

	bHasHandledOnActiveEvent = false;
	bHasHandledWhileActiveEvent = false;
	bInRecycleQueue = false;
	bAutoAttachToOwner = false;

	WarnIfLatentActionIsStillRunning = true;
	WarnIfTimelineIsStillRunning = true;
}

void AGameCueNotify_Actor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UGameCueManager::IsGameCueRecylingEnabled() && (EndPlayReason == EEndPlayReason::Destroyed))
	{
		UWorld* World = GetWorld();
		if (World && !World->IsPlayingReplay() && (World->WorldType == EWorldType::Game))
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("GameCueNotify [%s] is calling EndPlay(). This should not happen since they are recycled through the GameCueManager."), *GetName());
		}
	}

	if (EndPlayReason == EEndPlayReason::Destroyed)
	{
		UGMCAbilitySystemGlobals::Get().GetGameCueManager()->NotifyGameCueActorEndPlay(this);
	}

	Super::EndPlay(EndPlayReason);
}

void AGameCueNotify_Actor::K2_DestroyActor()
{
	if (UGameCueManager::IsGameCueRecylingEnabled())
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("GameCueNotify [%s] is calling DesotryActor(). This is not necessary as GCs will be cleaned up and recycled automatically."), *GetName());
	}
	else
	{
		Super::K2_DestroyActor();
	}
}

void AGameCueNotify_Actor::Destroyed()
{
	if (UGameCueManager::IsGameCueRecylingEnabled())
	{
		UWorld* World = GetWorld();
		if (World && !World->IsPlayingReplay() && (World->WorldType == EWorldType::Game))
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("GameCueNotify [%s] is calling Destroyed(). This should not happen since they are recycled through the GameCueManager."), *GetName());
		}
	}

	Super::Destroyed();
}

#if WITH_EDITOR
void AGameCueNotify_Actor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GetClass());

	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(AGameCueNotify_Actor, GameCueTag))
	{
		DeriveGameCueTagFromAssetName();
		UGMCAbilitySystemGlobals::Get().GetGameCueManager()->HandleAssetDeleted(Blueprint);
		UGMCAbilitySystemGlobals::Get().GetGameCueManager()->HandleAssetAdded(Blueprint);
	}
}
#endif

void AGameCueNotify_Actor::DeriveGameCueTagFromAssetName()
{
	UGMCAbilitySystemGlobals::DeriveGameCueTagFromClass<AGameCueNotify_Actor>(this);
}

void AGameCueNotify_Actor::Serialize(FArchive& Ar)
{
	if (Ar.IsSaving())
	{
		DeriveGameCueTagFromAssetName();
	}

	Super::Serialize(Ar);

	if (Ar.IsLoading())
	{
		DeriveGameCueTagFromAssetName();
	}
}

void AGameCueNotify_Actor::BeginPlay()
{
	Super::BeginPlay();
	AttachToOwnerIfNecessary();
}

void AGameCueNotify_Actor::SetOwner( AActor* InNewOwner )
{
	// Remove our old delegate
	ClearOwnerDestroyedDelegate();

	Super::SetOwner(InNewOwner);
	if (AActor* NewOwner = GetOwner())
	{
		NewOwner->OnDestroyed.AddDynamic(this, &AGameCueNotify_Actor::OnOwnerDestroyed);
		AttachToOwnerIfNecessary();
	}
}

void AGameCueNotify_Actor::AttachToOwnerIfNecessary()
{
	if (AActor* MyOwner = GetOwner())
	{
		if (bAutoAttachToOwner)
		{
			AttachToActor(MyOwner, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
	}
}

void AGameCueNotify_Actor::ClearOwnerDestroyedDelegate()
{
	AActor* OldOwner = GetOwner();
	if (OldOwner)
	{
		OldOwner->OnDestroyed.RemoveDynamic(this, &AGameCueNotify_Actor::OnOwnerDestroyed);
	}
}

void AGameCueNotify_Actor::PostInitProperties()
{
	Super::PostInitProperties();
	DeriveGameCueTagFromAssetName();
}

bool AGameCueNotify_Actor::HandlesEvent(EGameCueEvent::Type EventType) const
{
	return true;
}

void AGameCueNotify_Actor::K2_EndGameCue()
{
	GameCueFinishedCallback();
}

int32 GameCueNotifyTagCheckOnRemove = 1;
static FAutoConsoleVariableRef CVarGameCueNotifyActorStacking(TEXT("AbilitySystem.GameCueNotifyTagCheckOnRemove"), GameCueNotifyTagCheckOnRemove, TEXT("Check that target no longer has tag when removing GamepalyCues"), ECVF_Default );

void AGameCueNotify_Actor::HandleGameCue(AActor* MyTarget, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
	//SCOPE_CYCLE_COUNTER(STAT_HandleGameCueNotifyActor);

	if (Parameters.MatchedTagName.IsValid() == false)
	{
		UE_LOG(LogGMCAbilitySystem, Warning, TEXT("GameCue parameter is none for %s"), *GetNameSafe(this));
	}

	// Handle multiple event gating
	{
		if (EventType == EGameCueEvent::OnActive && !bAllowMultipleOnActiveEvents && bHasHandledOnActiveEvent)
		{
			return;
		}

		if (EventType == EGameCueEvent::WhileActive && !bAllowMultipleWhileActiveEvents && bHasHandledWhileActiveEvent)
		{
			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("GameCue Notify %s WhileActive already handled, skipping this one."), *GetName());
			return;
		}

		if (EventType == EGameCueEvent::Removed && bHasHandledOnRemoveEvent)
		{
			return;
		}
	}

	// If cvar is enabled, check that the target no longer has the matched tag before doing remove logic. This is a simple way of supporting stacking, such that if an actor has two sources giving it the same GC tag, it will not be removed when the first one is removed.
	if (GameCueNotifyTagCheckOnRemove > 0 && EventType == EGameCueEvent::Removed)
	{
		if (IGameplayTagAssetInterface* TagInterface = Cast<IGameplayTagAssetInterface>(MyTarget))
		{
			if (TagInterface->HasMatchingGameplayTag(Parameters.MatchedTagName))
			{
				return;
			}			
		}
	}

	if (IsValid(MyTarget))
	{
		K2_HandleGameCue(MyTarget, EventType, Parameters);

		// Clear any pending auto-destroy that may have occurred from a previous OnRemove
		SetLifeSpan(0.f);

		switch (EventType)
		{
		case EGameCueEvent::OnActive:
			OnActive(MyTarget, Parameters);
			bHasHandledOnActiveEvent = true;
			break;

		case EGameCueEvent::WhileActive:
			WhileActive(MyTarget, Parameters);
			bHasHandledWhileActiveEvent = true;
			break;

		case EGameCueEvent::Executed:
			OnExecute(MyTarget, Parameters);
			break;

		case EGameCueEvent::Removed:
			bHasHandledOnRemoveEvent = true;
			OnRemove(MyTarget, Parameters);

			if (bAutoDestroyOnRemove)
			{
				if (AutoDestroyDelay > 0.f)
				{
					FTimerDelegate Delegate = FTimerDelegate::CreateUObject(this, &AGameCueNotify_Actor::GameCueFinishedCallback);
					GetWorld()->GetTimerManager().SetTimer(FinishTimerHandle, Delegate, AutoDestroyDelay, false);
				}
				else
				{
					GameCueFinishedCallback();
				}
			}
			break;
		};
	}
	else
	{
		UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Null Target called for event %d on GameCueNotifyActor %s"), (int32)EventType, *GetName() );
		if (EventType == EGameCueEvent::Removed)
		{
			// Make sure the removed event is handled so that we don't leak GC notify actors
			GameCueFinishedCallback();
		}
	}
}

void AGameCueNotify_Actor::OnOwnerDestroyed(AActor* DestroyedActor)
{
	if (bInRecycleQueue)
	{
		// We are already done
		return;
	}

	// May need to do extra cleanup in child classes
	GameCueFinishedCallback();
}

bool AGameCueNotify_Actor::OnExecute_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters)
{
	return false;
}

bool AGameCueNotify_Actor::OnActive_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters)
{
	return false;
}

bool AGameCueNotify_Actor::WhileActive_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters)
{
	if (IsHidden())
	{
		SetActorHiddenInGame(false);
	}

	return false;
}

bool AGameCueNotify_Actor::OnRemove_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters)
{
	if (!IsHidden())
	{
		SetActorHiddenInGame(true);
	}

	return false;
}

void AGameCueNotify_Actor::GameCueFinishedCallback()
{
	UWorld* MyWorld = GetWorld();
	if (MyWorld) // Teardown cases in PIE may cause the world to be invalid
	{
		if (FinishTimerHandle.IsValid())
		{
			MyWorld->GetTimerManager().ClearTimer(FinishTimerHandle);
			FinishTimerHandle.Invalidate();
		}

		// Make sure OnRemoved has been called at least once if WhileActive was called (for possible cleanup)
		if (bHasHandledWhileActiveEvent && !bHasHandledOnRemoveEvent)
		{
			// Force onremove to be called with null parameters
			bHasHandledOnRemoveEvent = true;
			OnRemove(nullptr, FGameCueParameters());
		}
	}
	
	UGMCAbilitySystemGlobals::Get().GetGameCueManager()->NotifyGameCueActorFinished(this);
}

bool AGameCueNotify_Actor::GameCuePendingRemove()
{
	return GetLifeSpan() > 0.f || FinishTimerHandle.IsValid() || !IsValid(this);
}

bool AGameCueNotify_Actor::Recycle()
{
	bHasHandledOnActiveEvent = false;
	bHasHandledWhileActiveEvent = false;
	bHasHandledOnRemoveEvent = false;
	CueInstigator = nullptr;
	CueSourceObject = nullptr;

	ClearOwnerDestroyedDelegate();
	if (FinishTimerHandle.IsValid())
	{
		FinishTimerHandle.Invalidate();
	}

	// End timeline components
	TInlineComponentArray<UTimelineComponent*> TimelineComponents(this);
	for (UTimelineComponent* Timeline : TimelineComponents)
	{
		if (Timeline)
		{
			// May be too spammy, but want to call visibility to this. Maybe make this editor only?
			if (Timeline->IsPlaying() && WarnIfTimelineIsStillRunning)
			{
				UE_LOG(LogGMCAbilitySystem,Warning, TEXT("GameCueNotify_Actor %s had active timelines when it was recycled."), *GetName());
			}

			Timeline->SetPlaybackPosition(0.f, false, false);
			Timeline->Stop();
		}
	}

	UWorld* MyWorld = GetWorld();
	if (MyWorld)
	{
		// Note, ::Recycle is called on CDOs too, so that even "new" GCs start off in a recycled state.
		// So, its ok if there is no valid world here, just skip the stuff that has to do with worlds.
		if (MyWorld->GetLatentActionManager().GetNumActionsForObject(this) && WarnIfLatentActionIsStillRunning)
		{
			// May be too spammy, but want ot call visibility to this. Maybe make this editor only?
			UE_LOG(LogGMCAbilitySystem,Warning, TEXT("GameCueNotify_Actor %s has active latent actions (Delays, etc) when it was recycled."), *GetName());
		}

		// End latent actions
		MyWorld->GetLatentActionManager().RemoveActionsForObject(this);

		if (FAbilitySystemTweaks::ClearCueNotifyTimers)
		{
			// End all timers
			MyWorld->GetTimerManager().ClearAllTimersForObject(this);
		}
	}

	// Clear owner, hide, detach from parent
	SetOwner(nullptr);
	SetActorHiddenInGame(true);
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	return true;
}

void AGameCueNotify_Actor::ReuseAfterRecycle()
{
	SetActorHiddenInGame(false);
}

