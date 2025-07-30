// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StateTreeGMASAIComponent.h"

#include "StateTreeExecutionContext.h"
#include "Components/StateTreeGMASAIComponentSchema.h"


bool UStateTreeGMASAIComponent::SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors)
{
	Context.SetCollectExternalDataCallback(FOnCollectStateTreeExternalData::CreateUObject(this, &UStateTreeGMASAIComponent::CollectExternalData));
	return UStateTreeGMASAIComponentSchema::SetContextRequirements(*this, Context, bLogErrors);
}


TSubclassOf<UStateTreeSchema> UStateTreeGMASAIComponent::GetSchema() const
{
	return UStateTreeGMASAIComponentSchema::StaticClass();
}
