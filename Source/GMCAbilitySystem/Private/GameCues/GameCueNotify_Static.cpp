// Copyright Epic Games, Inc. All Rights Reserved.

#include "GameCues/GameCueNotify_Static.h"
#include "Engine/Blueprint.h"
#include "GMCAbilitySystemGlobals.h"
#include "GMCAbilityComponent.h"
#include "GameCues/GMC_GameCueManager.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(GameCueNotify_Static)

UGameCueNotify_Static::UGameCueNotify_Static(const FObjectInitializer& PCIP)
: Super(PCIP)
{
	IsOverride = true;
}

#if WITH_EDITOR
void UGameCueNotify_Static::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FProperty* PropertyThatChanged = PropertyChangedEvent.Property;
	UBlueprint* Blueprint = UBlueprint::GetBlueprintFromClass(GetClass());

	if (PropertyThatChanged && PropertyThatChanged->GetFName() == GET_MEMBER_NAME_CHECKED(UGameCueNotify_Static, GameCueTag))
	{
		DeriveGameCueTagFromAssetName();
        
		UGMCAbilitySystemGlobals* AbilitySystemGlobals = &UGMCAbilitySystemGlobals::Get();
		if (AbilitySystemGlobals && AbilitySystemGlobals->GetGameCueManager())
		{
			AbilitySystemGlobals->GetGameCueManager()->HandleAssetDeleted(Blueprint);
			AbilitySystemGlobals->GetGameCueManager()->HandleAssetAdded(Blueprint);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Failed to access GameCueManager or UGMCAbilitySystemGlobals is null"));
		}
	}
}
#endif

void UGameCueNotify_Static::DeriveGameCueTagFromAssetName()
{
	UGMCAbilitySystemGlobals::DeriveGameCueTagFromClass<UGameCueNotify_Static>(this);
}

void UGameCueNotify_Static::Serialize(FArchive& Ar)
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

void UGameCueNotify_Static::PostInitProperties()
{
	Super::PostInitProperties();
	DeriveGameCueTagFromAssetName();
}

bool UGameCueNotify_Static::HandlesEvent(EGameCueEvent::Type EventType) const
{
	return true;
}

void UGameCueNotify_Static::HandleGameCue(AActor* MyTarget, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters)
{
//	SCOPE_CYCLE_COUNTER(STAT_HandleGameCueNotifyStatic);

	if (IsValid(MyTarget))
	{
		K2_HandleGameCue(MyTarget, EventType, Parameters);

		switch (EventType)
		{
		case EGameCueEvent::OnActive:
			OnActive(MyTarget, Parameters);
			break;

		case EGameCueEvent::WhileActive:
			WhileActive(MyTarget, Parameters);
			break;

		case EGameCueEvent::Executed:
			OnExecute(MyTarget, Parameters);
			break;

		case EGameCueEvent::Removed:
			OnRemove(MyTarget, Parameters);
			break;
		};
	}
	else
	{
		UE_LOG(LogGMCAbilitySystem,Warning, TEXT("Null Target"));
	}
}

void UGameCueNotify_Static::OnOwnerDestroyed()
{
}

bool UGameCueNotify_Static::OnExecute_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters) const
{
	return false;
}

bool UGameCueNotify_Static::OnActive_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters) const
{
	return false;
}

bool UGameCueNotify_Static::WhileActive_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters) const
{
	return false;
}

bool UGameCueNotify_Static::OnRemove_Implementation(AActor* MyTarget, const FGameCueParameters& Parameters) const
{
	return false;
}

UWorld* UGameCueNotify_Static::GetWorld() const
{
	return UGameCueManager::GetCachedWorldForGameCueNotifies();
}

