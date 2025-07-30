// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StateTreeGMASAIComponentSchema.h"

#include "AIController.h"
#include "BrainComponent.h"
#include "GMCPawn.h"
#include "Conditions/StateTreeAIConditionBase.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Tasks/StateTreeAITask.h"

UStateTreeGMASAIComponentSchema::UStateTreeGMASAIComponentSchema(const FObjectInitializer& ObjectInitializer) : AIControllerClass(AAIController::StaticClass())
{
	check(ContextDataDescs.Num() == 2 && ContextDataDescs[0].Struct == AGMC_Pawn::StaticClass());
	// Make the Actor a pawn by default so it binds to the controlled pawn instead of the AIController.
	ContextDataDescs[0].Struct = ContextActorClass.Get();
	ContextDataDescs.Emplace(TEXT("AIController"), AIControllerClass.Get(), FGuid(0xEDB3CD97, 0x95F94E0A, 0xBD15207B, 0x98645CDC));
}

void UStateTreeGMASAIComponentSchema::PostLoad()
{
	Super::PostLoad();
	ContextDataDescs[1].Struct = AbilitySystemComponentClass.Get();
	ContextDataDescs[2].Struct = AIControllerClass.Get();
}

bool UStateTreeGMASAIComponentSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return Super::IsStructAllowed(InScriptStruct)
	|| InScriptStruct->IsChildOf(FStateTreeAITaskBase::StaticStruct())
	|| InScriptStruct->IsChildOf(FStateTreeAIConditionBase::StaticStruct());
}

bool UStateTreeGMASAIComponentSchema::SetContextRequirements(UBrainComponent& BrainComponent, FStateTreeExecutionContext& Context, bool bLogErrors)
{
	if (!Context.IsValid())
	{
		return false;
	}

	const FName AIControllerName(TEXT("AIController"));
	Context.SetContextDataByName(AIControllerName, FStateTreeDataView(BrainComponent.GetAIOwner()));

	return Super::SetContextRequirements(BrainComponent, Context, bLogErrors);
}

#if WITH_EDITOR
void UStateTreeGMASAIComponentSchema::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		if (Property->GetOwnerClass() == UStateTreeGMASAIComponentSchema::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeGMASAIComponentSchema, AIControllerClass))
		{
			ContextDataDescs[1].Struct = AIControllerClass.Get();
		}
	}
}
#endif // WITH_EDITOR