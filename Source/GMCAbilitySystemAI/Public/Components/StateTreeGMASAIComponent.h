// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeGMASComponent.h"
#include "StateTreeGMASAIComponent.generated.h"


UCLASS(MinimalAPI, DisplayName="StateTreeGMAS AI", ClassGroup = AI, Blueprintable, meta = (BlueprintSpawnableComponent))
class UStateTreeGMASAIComponent : public UStateTreeGMASComponent
{
	GENERATED_BODY()

	virtual bool SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors = false) override;

public:
	//~ BEGIN IStateTreeSchemaProvider
	GMCABILITYSYSTEMAI_API TSubclassOf<UStateTreeSchema> GetSchema() const override;
	//~ END
};
