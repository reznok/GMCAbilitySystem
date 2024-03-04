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
	GENERATED_BODY()
	
	UPROPERTY()
	int AbilityActivationID{0};
	
	// Ability ID to cast
	UPROPERTY()
	FGameplayTag AbilityTag;

	// The input used to start the ability on the client
	// Needed for things like "WaitForKeyRelease"
	UPROPERTY()
	UInputAction* ActionInput;

	bool operator==(const FGMCAbilityData& Other) const { return AbilityActivationID == Other.AbilityActivationID && AbilityTag == Other.AbilityTag;}

};
