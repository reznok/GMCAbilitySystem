// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Attributes.h"
#include "UObject/Object.h"
#include "GMCAbilityEffect.generated.h"

USTRUCT(BlueprintType)
struct FAttributeModifier
{
	GENERATED_BODY()
	
	UPROPERTY(EditDefaultsOnly)
	FName AttributeName;

	UPROPERTY(EditDefaultsOnly)
	float Value;
};


/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY(EditDefaultsOnly);
	TArray<FAttributeModifier> Modifiers;
};

