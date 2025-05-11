// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/StateTreeGMASComponentSchema.h"

#include "BrainComponent.h"
#include "GMCAbilitySystemAI.h"
#include "GMCPawn.h"
#include "StateTreeExecutionContext.h"
#include "Conditions/StateTreeGMASConditionBase.h"
#include "Tasks/StateTreeGMASTask.h"

UStateTreeGMASComponentSchema::UStateTreeGMASComponentSchema(const FObjectInitializer& ObjectInitializer)
{
	check(ContextDataDescs.Num() == 1 && ContextDataDescs[0].Struct == AActor::StaticClass());
	// Make the Actor a pawn by default so it binds to the controlled pawn instead of the AIController.
	ContextActorClass = AGMC_Pawn::StaticClass();
	ContextDataDescs[0].Struct = ContextActorClass.Get();
	ContextDataDescs.Emplace(TEXT("AbilitySystemComponent"), AbilitySystemComponentClass.Get(), FGuid(0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF, 0xDEADBEEF));
}

void UStateTreeGMASComponentSchema::PostLoad()
{
	Super::PostLoad();
	ContextDataDescs[1].Struct = AbilitySystemComponentClass.Get();
}

bool UStateTreeGMASComponentSchema::IsStructAllowed(const UScriptStruct* InScriptStruct) const
{
	return Super::IsStructAllowed(InScriptStruct)
		|| InScriptStruct->IsChildOf(FStateTreeGMASTaskBase::StaticStruct())
		|| InScriptStruct->IsChildOf(FStateTreeGMASConditionBase::StaticStruct());
}

bool UStateTreeGMASComponentSchema::SetContextRequirements(UBrainComponent& BrainComponent, FStateTreeExecutionContext& Context, bool bLogErrors)
{

	// GMAS State Tree only works on the server
	const UWorld* World = BrainComponent.GetWorld();
	if (World)
	{
		const ENetMode NetMode = World->GetNetMode();
		if (NetMode >= NM_Client)
		{
			return false;
		}
	}
	
	if (!Context.IsValid())
	{
		return false;
	}

	const FName ASCName(TEXT("AbilitySystemComponent"));
	
	UGMC_AbilitySystemComponent* ASC = nullptr;
	AActor* OwnerActor = Cast<AActor>(BrainComponent.GetOwner());
	if (OwnerActor)
	{
		ASC = Cast<UGMC_AbilitySystemComponent>(OwnerActor->GetComponentByClass<UGMC_AbilitySystemComponent>());
	}

	if (ASC == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Error, TEXT("StateTreeGMASComponentSchema: Failed to find AbilitySystemComponent on %s"), *OwnerActor->GetName());
	}
	
	Context.SetContextDataByName(ASCName, ASC);

	return Super::SetContextRequirements(BrainComponent, Context, bLogErrors);
}

#if WITH_EDITOR
void UStateTreeGMASComponentSchema::PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent)
{
	Super::PostEditChangeChainProperty(PropertyChangedEvent);

	if (FProperty* Property = PropertyChangedEvent.Property)
	{
		if (Property->GetOwnerClass() == UStateTreeGMASComponentSchema::StaticClass()
			&& Property->GetFName() == GET_MEMBER_NAME_CHECKED(UStateTreeGMASComponentSchema, AbilitySystemComponentClass))
		{
			ContextDataDescs[1].Struct = AbilitySystemComponentClass.Get();
		}
	}
}
#endif
