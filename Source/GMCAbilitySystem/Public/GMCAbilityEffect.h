// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GMCAbilityEffect.generated.h"

UENUM(BlueprintType)
enum class EEffectType : uint8
{
	Instant,  // Applies Instantly
	Duration, // Lasts for X time
	Infinite  // Lasts forever
};

UENUM(BlueprintType)
enum class EEffectState : uint8
{
	Initialized,  // Applies Instantly
	Started, // Lasts for X time
	Ended  // Lasts forever
};

USTRUCT(BlueprintType)
struct FGMCAttributeModifier
{
	GENERATED_BODY()

	FGMCAttributeModifier()
	{
		Value = 0;
	}
	
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	FName AttributeName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite)
	float Value;
};

USTRUCT(BlueprintType)
struct FGMCAbilityEffectData
{
	GENERATED_BODY()

	FGMCAbilityEffectData(): EffectID(0), ServerStartTime(0), ServerEndTime(0), Duration(0),
	                         bOverwriteExistingModifiers(false)
	{
	}

	UPROPERTY()
	int EffectID;

	UPROPERTY()
	double ServerStartTime;
	
	UPROPERTY()
	double ServerEndTime;

	UPROPERTY()
	double Duration;

	// Whether or not overwrite existing modifiers with additional ones of the same name
	UPROPERTY()
	bool bOverwriteExistingModifiers;

	// Non default modifier data
	UPROPERTY(BlueprintReadWrite)
	TArray<FGMCAttributeModifier> Modifiers;
	
	inline bool operator==(const FGMCAbilityEffectData& Other) const
	{
		return ServerStartTime == Other.ServerStartTime && ServerEndTime == Other.ServerEndTime;
	};
};

/**
 * 
 */
class UGMC_AbilityComponent;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly)
	EEffectType EffectType;

	EEffectState CurrentState;

	UPROPERTY(EditDefaultsOnly)
	double Duration;

	// How often to apply this Effect in seconds
	// 0 Will apply only at the start
	UPROPERTY(EditDefaultsOnly)
	float Period;

	// Number of seconds to wait before this effect starts
	// This is added to the RTT betweeen server/client
	UPROPERTY(EditDefaultsOnly)
	double Delay;

	// Window that a client can predict an effect application/removal
	// This will add time before and after the Effect, so a Tolerance of .2 creates
	// a .4 window for effect state changes.
	UPROPERTY(EditDefaultsOnly)
	double LagTolerance;

	// Automatically try to determine an acceptable lag tolerance for this effect/pawn
	UPROPERTY(EditDefaultsOnly)
	bool bAutoDetermineLagTolerance = true;

	UPROPERTY(EditDefaultsOnly);
	TArray<FGMCAttributeModifier> Modifiers;
	
	void InitializeEffect(UGMC_AbilityComponent* AbilityComponent, bool bServerApplied = false, FGMCAbilityEffectData InitializationData = {});
	
	void EndEffect();
	double GetEndTime(){return StartTime + Duration;}
	double GetStartTime(){return StartTime; };
	
	void Tick(float DeltaTime);
	void TickPeriodicEffects(float DeltaTime);

	void UpdateState(EEffectState State, bool Force=false);

	bool CompletedAndServerConfirmed();
	
	// Has Ability completed and should be cleaned up?
	bool bCompleted;

	bool bServerConfirmed;
	
private:
	
	// Only set by the server, Predicted effects will not have an ID until they become Active effects
	// Used to pass effect data from the server down to the client
	float Id = -1;

	bool bHasStarted;

	double StartTime;
	double EndTime;

	UPROPERTY()
	UGMC_AbilityComponent* OwnerAbilityComponent;
	
	void CheckState();

	float PeriodicApplicationTimer = 0;
};

