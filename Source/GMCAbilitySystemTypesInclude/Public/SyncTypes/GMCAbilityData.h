// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "UObject/Object.h"
#include "GMCAbilityData.generated.h"

/**
 * 
 */
USTRUCT(BlueprintType)
struct FGMCAbilityData
{
	GENERATED_USTRUCT_BODY()
	
	bool operator==(const FGMCAbilityData& Rhs) const { return AbilityActivationID == Rhs.AbilityActivationID && AbilityTag == Rhs.AbilityTag && bProcessed == Rhs.bProcessed && ActionInput == Rhs.ActionInput && bProgressTask == Rhs.bProgressTask && TaskID == Rhs.TaskID;}
	bool operator!=(const FGMCAbilityData& Rhs) const { return !operator==(Rhs); }
	
	FString ToStringSimple() const;
	
	// ID to reference the specific ability data
	// https://forums.unrealengine.com/t/fs-test-id-is-not-initialized-properly/560690/5 for why the meta is needed
	UPROPERTY(BlueprintReadOnly, meta = (IgnoreForMemberInitializationTest))
	int AbilityActivationID{AbilityActivationIDCounter++};

	inline static int AbilityActivationIDCounter;
	
	// Ability ID to cast
	UPROPERTY(BlueprintReadWrite)
	FGameplayTag AbilityTag;
		
	bool bProcessed{true};

	// The input used to start the ability on the client
	// Needed for things like "WaitForKeyRelease"
	UPROPERTY()
	UInputAction* ActionInput{nullptr};
	
	// Used to continue execution of blueprints with waiting latent nodes where client can progress execution
	// Ie: Waiting for a key to be released
	UPROPERTY(BlueprintReadWrite)
	bool bProgressTask{false};

	// Task to continue ability execution on
	UPROPERTY(BlueprintReadWrite)
	int TaskID{-1};

	bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess);
};

inline bool FGMCAbilityData::NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess)
{
	Ar << AbilityActivationID;
	Ar << AbilityTag;
	Ar << bProcessed;
	Ar << ActionInput;
	Ar << bProgressTask;
	Ar << TaskID;
	bOutSuccess = true;
	return true;
}