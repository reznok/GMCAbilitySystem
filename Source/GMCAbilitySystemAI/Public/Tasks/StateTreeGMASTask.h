// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeTaskBase.h"
#include "StateTreeGMASTask.generated.h"

/**
 * 
 */
// Base class of all AI task that expect to be run on an AIController or derived class
USTRUCT(meta= (Hidden, Category = "GMAS"))
struct GMCABILITYSYSTEMAI_API FStateTreeGMASTaskBase : public FStateTreeTaskBase
{
	GENERATED_BODY()
};

// Base class of all AI task that do a physical action 
USTRUCT(meta = (Hidden, Category="GMAS|Action"))
struct GMCABILITYSYSTEMAI_API FStateTreeGMASActionTaskBase : public FStateTreeGMASTaskBase
{
	GENERATED_BODY()
};