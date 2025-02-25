// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "InputAction.h"
#include "UObject/Object.h"
#include "StructUtils/InstancedStruct.h" 
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
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	FGameplayTag InputTag = FGameplayTag::EmptyTag;

	// The input used to start the ability on the client
	// Needed for things like "WaitForKeyRelease"
	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	TObjectPtr<const UInputAction> ActionInput;

	UPROPERTY(BlueprintReadOnly, Category = "GMCAbilitySystem")
	float time = 0.0f;

	FInstancedStruct payload;


	bool operator==(const FGMCAbilityData& Other) const { return AbilityActivationID == Other.AbilityActivationID && InputTag == Other.InputTag;}
	bool operator!=(const FGMCAbilityData& Other) const { return *this == Other;}

};
