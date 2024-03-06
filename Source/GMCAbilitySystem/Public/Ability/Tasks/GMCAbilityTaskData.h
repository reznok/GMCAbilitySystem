// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GMCAbilityTaskData.generated.h"

UENUM()
enum EGMCAbilityTaskDataType : uint8
{
	None,
	Heartbeat,
	Progress
};


USTRUCT()
struct GMCABILITYSYSTEM_API FGMCAbilityTaskData
{
	GENERATED_BODY()
	// Ability this Task is running under
	UPROPERTY()
	int AbilityID{-1};

	// Task to continue ability execution on
	UPROPERTY()
	int TaskID{-1};

	UPROPERTY()
	TEnumAsByte<EGMCAbilityTaskDataType> TaskType{EGMCAbilityTaskDataType::None};
	
	bool operator==(const FGMCAbilityTaskData& Other) const {return AbilityID == Other.AbilityID && TaskID == Other.TaskID && TaskType == Other.TaskType;};
	bool operator!=(const FGMCAbilityTaskData& Other) const {return AbilityID != Other.AbilityID || TaskID != Other.TaskID || TaskType != Other.TaskType;};
};
