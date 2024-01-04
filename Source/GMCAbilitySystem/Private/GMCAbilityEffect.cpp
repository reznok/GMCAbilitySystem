// Fill out your copyright notice in the Description page of Project Settings.


#include "GMCAbilityEffect.h"

#include "GMCPlayerController.h"
#include "WorldTime.h"
#include "Components/GMCAbilityComponent.h"
#include "Kismet/GameplayStatics.h"

void UGMCAbilityEffect::InitializeEffect(UGMC_AbilityComponent* AbilityComponent, bool bServerApplied, FGMCAbilityEffectData InitializationData)
{
	if (AbilityComponent)
	{
		OwnerAbilityComponent = AbilityComponent;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid AbilityComponent Passed to UGMCAbilityEffect"));
	}

	// Add any programmatically generated modifiers
	for (FGMCAttributeModifier Modifier : InitializationData.Modifiers)
	{
		if (InitializationData.bOverwriteExistingModifiers)
		{
			TArray<int> ExistingModifiersToRemove;
			// Find all matching with the same name
			for (int i = 0; i < Modifiers.Num(); i++)
			{
				if (Modifiers[i].AttributeName == Modifier.AttributeName)
				{
					ExistingModifiersToRemove.Push(i);
				}
			}
			// Purge them from the existing modifiers array
			for (int i : ExistingModifiersToRemove)
			{
				Modifiers.RemoveAt(i);
			}
		}
		
		Modifiers.Add(Modifier);
	}

	if (AbilityComponent->HasAuthority())
	{
		// Server confirms its self!
		bServerConfirmed = true;
		// If applied by server, for the client to be able to actually predict this, the start of the ability must be delayed
		// on the server long enough for the client to receive the effect. 
		if (bServerApplied)
		{
			float RTT = GetNetworkRoundTripTimeSeconds();
			StartTime = Delay + GetReplicatedTime() + GetNetworkRoundTripTimeSeconds();
			EndTime = StartTime + Duration + GetNetworkRoundTripTimeSeconds();

			if (bAutoDetermineLagTolerance)
			{
				// Some rough estimates from personal testing
				if (GetNetworkRoundTripTimeSeconds() * 1000 <= 100){ LagTolerance = .5;}
				else { LagTolerance = .5;}
			}
		}
		else
		{
			StartTime = GetReplicatedTime();
			EndTime = GetReplicatedTime() + Duration;
			UpdateState(EEffectState::Started);
		}
		return;
	}
	
	// This is a client prediction
	if (InitializationData == FGMCAbilityEffectData{})
	{
		StartTime = GetReplicatedTime();
		EndTime = GetReplicatedTime() + Duration;
		UpdateState(EEffectState::Started);
	}
	// Data from the server about this effect has been received
	else
	{
		// Actual delay is a bit longer than RTT, hence the 30% increase
		StartTime = InitializationData.ServerStartTime - GetNetworkRoundTripTimeSeconds() - .1;
		UE_LOG(LogTemp, Warning, TEXT("Time Until Client Start: %lf"), StartTime - GetReplicatedTime() )
		EndTime = InitializationData.ServerEndTime - GetNetworkRoundTripTimeSeconds();
		Id = InitializationData.EffectID;
		bServerConfirmed = true;
		// OwnerAbilityComponent->ClientPredictEffectStateChange(Id, EEffectState::Started);
		
	}
}

void UGMCAbilityEffect::EndEffect()
{
	if (bCompleted) return;
	// Instant effects are "permanent"
	// Periodic effects are just repeated instant effects
	if (EffectType != EEffectType::Instant && Period == 0)
	{
		OwnerAbilityComponent->RemoveActiveAbilityModifiers(this);
	}
	bCompleted = true;
}

void UGMCAbilityEffect::Tick(float DeltaTime)
{
	TickPeriodicEffects(DeltaTime);
	
	if (!bHasStarted)
	{
		CheckState(EEffectState::Started);
	}
	else
	{
		if (!bCompleted && EffectType != EEffectType::Infinite)
		{
			CheckState(EEffectState::Ended);
			bCompleted = true;
		}
	}
	
}

void UGMCAbilityEffect::TickPeriodicEffects(float DeltaTime)
{
	// Cannot currently predict periodic effects
	if (!OwnerAbilityComponent->HasAuthority()) return;
	
	// If there's a period, check to see if it needs to apply its modifiers
	if (Period != 0 && CurrentState == EEffectState::Started)
	{
		PeriodicApplicationTimer += DeltaTime;
		if (PeriodicApplicationTimer >= Period)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifiers(this);
			PeriodicApplicationTimer = 0;

			UE_LOG(LogTemp, Warning, TEXT("Tick"));
		}
	}
}

void UGMCAbilityEffect::UpdateState(EEffectState State, bool Force)
{
	// Client prediction, apply state immediately
	if (!OwnerAbilityComponent->HasAuthority() || Force)
	{
		CurrentState = State;
		return;
	}
	
	if (OwnerAbilityComponent->HasAuthority())
	{
		// Check it's within tolerance
		if (CurrentState == EEffectState::Initialized && (GetReplicatedTime() + LagTolerance >= StartTime ||
															GetReplicatedTime() - LagTolerance <= StartTime))
		{
			UE_LOG(LogTemp, Warning, TEXT("Request To Start Was In Tolerance"));
			CurrentState = State;
		}		
		else if (CurrentState == EEffectState::Started && (GetReplicatedTime() + LagTolerance >= EndTime ||
															GetReplicatedTime() - LagTolerance <= EndTime))
		{
			UE_LOG(LogTemp, Warning, TEXT("Request To End Was In Tolerance"));
			CurrentState = State;
			// bCompleted = true;
		}
	}

}

bool UGMCAbilityEffect::CompletedAndServerConfirmed()
{
	return bCompleted && bServerConfirmed;
}

double UGMCAbilityEffect::GetNetworkRoundTripTimeSeconds()
{
	if (AGMC_PlayerController* PC = Cast<AGMC_PlayerController>(OwnerAbilityComponent->GetOwner()->GetNetOwningPlayer()->GetPlayerController(GetWorld())))
	{
		return PC->GetPingInMilliseconds() / 1000;
	}
	
	UE_LOG(LogTemp, Error, TEXT("GMC PlayerController Not Found On Pawn"));
	return 0;
}

double UGMCAbilityEffect::GetReplicatedTime()
{
	TArray<AActor*> Actors{};
	UGameplayStatics::GetAllActorsOfClass(OwnerAbilityComponent->GetWorld(), AGMC_WorldTimeReplicator::StaticClass(), Actors);

	if (Actors.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Error finding GMC_WorldTimeReplicator"));
		return -1;
	}
	AGMC_WorldTimeReplicator* WorldTimeReplicator = Cast<AGMC_WorldTimeReplicator>(Actors[0]);
	if (!WorldTimeReplicator)
	{
		UE_LOG(LogTemp, Error, TEXT("Error casting GMC_WorldTimeReplicator"));
		return -1;
	}
	
	return WorldTimeReplicator->GetRealWorldTimeSecondsReplicated();
}

void UGMCAbilityEffect::CheckState(EEffectState State)
{
	const double TimeToCheck = (State == EEffectState::Started ? StartTime : EndTime);
	
	if (CurrentState == State)
	{
		// Add the modifiers from the effect at start
		if (!bHasStarted && State == EEffectState::Started)
		{
			OwnerAbilityComponent->ApplyAbilityEffectModifiers(this);
			bHasStarted = true;

			// Instant Effects are instantly ended
			if (EffectType == EEffectType::Instant)
			{
				UpdateState(EEffectState::Ended, true);
			}
		}
		else if (State == EEffectState::Ended)
		{
			OwnerAbilityComponent->RemoveActiveAbilityEffect(this);
		}
	}
	// Client predict effect state change
	else if (!OwnerAbilityComponent->HasAuthority() && TimeToCheck <= GetReplicatedTime())
	{
		OwnerAbilityComponent->ClientPredictEffectStateChange(Id, State);
	}
	
	// If client hasn't predicted the effect within lag tolerance, update the state anyways
	// This provides mostly server auth
	else if (OwnerAbilityComponent->HasAuthority() && TimeToCheck + LagTolerance <= GetReplicatedTime())
	{
		UE_LOG(LogTemp, Warning, TEXT("Forcing state forward..."));
		UpdateState(State, true);
	}
}
