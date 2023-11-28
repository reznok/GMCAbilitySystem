// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Attributes.h"
#include "UObject/Object.h"
#include "GMCAbilityEffect.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbilityEffect : public UObject
{
	GENERATED_BODY()
	

public:
	/** Name of the attribute, usually the same as property name */
	UPROPERTY(EditDefaultsOnly)
	FName AttributeName;

	UPROPERTY(EditDefaultsOnly)
	float  Modifier;

//	UPROPERTY()
//	FProperty* Attribute;
};
