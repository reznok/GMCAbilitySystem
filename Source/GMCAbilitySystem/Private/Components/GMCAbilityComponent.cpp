// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"


// Sets default values for this component's properties
UGMC_AbilityComponent::UGMC_AbilityComponent()
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;
	// ...
}

void UGMC_AbilityComponent::BindReplicationData_Implementation()
{
	Super::BindReplicationData_Implementation();

	// Attribute Binds
	//
	InstantiateAttributes();
	for (const FAttribute* Attribute : Attributes->GetAllAttributes())
	{
		BindSinglePrecisionFloat(Attribute->Value,
			EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::Linear);
	}
	//
	// AbilityData Binds
	// These are mostly client-inputs made to the server as Ability Requests
	BindInt(AbilityData.AbilityActivationID,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindBool(AbilityData.bProgressTask,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindInt(AbilityData.TaskID,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindInt(AbilityData.GrantedAbilityIndex,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindCompressedVector(AbilityData.TargetVector0,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindCompressedVector(AbilityData.TargetVector1,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindCompressedVector(AbilityData.TargetVector2,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindActorReference(AbilityData.TargetActor,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindActorComponentReference(AbilityData.TargetComponent,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindBool(AbilityData.bProcessed,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);
		
	BindBool(bJustTeleported,
		EGMC_PredictionMode::ServerAuth_Output_ClientValidated,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::PeriodicAndOnChange_Output,
		EGMC_InterpolationFunction::TargetValue);
	}

void UGMC_AbilityComponent::GenAncillaryTick_Implementation(float DeltaTime, bool bIsCombinedClientMove)
{
	Super::GenAncillaryTick_Implementation(DeltaTime, bIsCombinedClientMove);
	OnFGMCAbilitySystemComponentTickDelegate.Broadcast(DeltaTime);
	
}

bool UGMC_AbilityComponent::TryActivateAbility(FGMCAbilityData InAbilityData)
{
	// UE_LOG(LogTemp, Warning, TEXT("Trying To Activate Ability: %d"), AbilityData.GrantedAbilityIndex);
	if (AbilityData.GrantedAbilityIndex >= 0 && AbilityData.GrantedAbilityIndex <= GrantedAbilities.Num() - 1)
	{
		UGMCAbility* Ability = NewObject<UGMCAbility>(this, GrantedAbilities[AbilityData.GrantedAbilityIndex]);
		ActiveAbilities.Add(InAbilityData.AbilityActivationID, Ability);

		// Check to make sure there's enough resource to use
		if (!CanAffordAbilityCost(Ability)) return false;
		
		Ability->Execute(InAbilityData, this);

		return true;
		// UE_LOG(LogTemp, Warning, TEXT("Ability: %d Activated | %s"), AbilityData.GrantedAbilityIndex, *AbilityData.AbilityActivationID.ToString());
	}

	return false;
}

void UGMC_AbilityComponent::QueueAbility(const FGMCAbilityData& InAbilityData)
{
	UE_LOG(LogTemp, Warning, TEXT("Queued Ability: %d"), AbilityData.GrantedAbilityIndex);
	QueuedAbilities.Push(InAbilityData);
}

void UGMC_AbilityComponent::GenPredictionTick_Implementation(float DeltaTime)
{
	Super::GenPredictionTick_Implementation(DeltaTime);
	
	bJustTeleported = false;
	CleanupStaleAbilities();
	
	if (AbilityData.bProcessed == false)
	{
		if (AbilityData.bProgressTask)
		{
			// UE_LOG(LogTemp, Warning, TEXT("%d: Continuing Ability..."), GetOwner()->GetLocalRole());

			if (ActiveAbilities.Contains(AbilityData.AbilityActivationID))
			{
				// UE_LOG(LogTemp, Warning, TEXT("%d: Found Ability to continue!"), GetOwner()->GetLocalRole());
				ActiveAbilities[AbilityData.AbilityActivationID]->CompleteLatentTask(AbilityData.TaskID);
			}
		}
		else
		{
			if (!ActiveAbilities.Contains(AbilityData.AbilityActivationID))
			{
				TryActivateAbility(AbilityData);
				// UE_LOG(LogTemp, Warning, TEXT("%d: Processing Ability..."), GetOwner()->GetLocalRole());
			}
		}
		AbilityData.bProcessed = true;
	}
}

void UGMC_AbilityComponent::GenSimulationTick_Implementation(float DeltaTime)
{
	Super::GenSimulationTick_Implementation(DeltaTime);

	if (GetSmoothingTargetIdx() == -1) return;	
	
	const FVector TargetLocation = MoveHistory[GetSmoothingTargetIdx()].OutputState.ActorLocation.Read();
	if (bJustTeleported)
	{
		// UE_LOG(LogTemp, Warning, TEXT("Teleporting %f Units"), FVector::Distance(GetOwner()->GetActorLocation(), TargetLocation));
		GetOwner()->SetActorLocation(TargetLocation);
		bJustTeleported = false;
	}
}

void UGMC_AbilityComponent::PreLocalMoveExecution_Implementation(const FGMC_Move& LocalMove)
{
	Super::PreLocalMoveExecution_Implementation(LocalMove);

	if (QueuedAbilities.Num() > 0)
	{
		AbilityData = QueuedAbilities.Pop();
		AbilityData.bProcessed = false;
	}
}

void UGMC_AbilityComponent::BeginPlay()
{
	Super::BeginPlay();
}


bool UGMC_AbilityComponent::CanAffordAbilityCost(UGMCAbility* Ability)
{
	UGMCAbilityEffect* AbilityEffect = Ability->AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	for (FAttributeModifier AttributeModifier : AbilityEffect->Modifiers)
	{
		if (const FAttribute* AffectedAttribute = Attributes->GetAttributeByName(AttributeModifier.AttributeName))
		{
			// If current - proposed is less than 0, cannot afford. Cost values are negative, so these are added.
			if (AffectedAttribute->Value + AttributeModifier.Value < 0) return false;
		}
	}

	return true;
}

void UGMC_AbilityComponent::InstantiateAttributes()
{
	// Use BP'd Attributes if provided. Else use default attributes (which is nothing by default)
	if (StartingAttributes)
	{
		Attributes = NewObject<UGMCAttributeSet>(this, StartingAttributes);
	}
	else if (Attributes == nullptr)
	{
		Attributes = NewObject<UGMCAttributeSet>();
	}
}

void UGMC_AbilityComponent::CleanupStaleAbilities()
{
	TArray<int> MarkedForDeletion;
	for (const TPair<int, UGMCAbility*>& Ability : ActiveAbilities)
	{
		if (Ability.Value->AbilityState == EAbilityState::Ended)
		{
			MarkedForDeletion.Push(Ability.Key);
		}
	}

	for (const int AbilityActivationID : MarkedForDeletion)
	{
		ActiveAbilities.Remove(AbilityActivationID);
		// UE_LOG(LogTemp, Warning, TEXT("%d: Cleaned Up Ability: %d"),GetOwner()->GetLocalRole(),  AbilityActivationID);
	}
}

void UGMC_AbilityComponent::SetAttributes(UGMCAttributeSet* NewAttributes)
{
	this->Attributes = NewAttributes;
}

void UGMC_AbilityComponent::ApplyAbilityCost(UGMCAbility* Ability)
{

	if (Ability->AbilityCost != nullptr)
	{
		ApplyAbilityEffect(Ability->AbilityCost);
	}
}

void UGMC_AbilityComponent::ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect)
{
	UGMCAbilityEffect* AbilityEffect = Effect->GetDefaultObject<UGMCAbilityEffect>();

	for (FAttributeModifier AttributeModifier : AbilityEffect->Modifiers)
	{
		if (const FAttribute* AffectedAttribute = Attributes->GetAttributeByName(AttributeModifier.AttributeName))
		{
			AffectedAttribute->Value += AttributeModifier.Value;
		}
	}
}



