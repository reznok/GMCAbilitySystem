// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/GMCAbilityComponent.h"

#include "GMCPlayerController.h"
#include "WorldTime.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/ObjectLibrary.h"
#include "Kismet/GameplayStatics.h"


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
			EGMC_PredictionMode::ServerAuth_Output_ServerValidated,
			EGMC_CombineMode::CombineIfUnchanged,
			EGMC_SimulationMode::Periodic_Output,
			EGMC_InterpolationFunction::TargetValue);
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

	// Effect State binds
	BindInt(EffectStatePrediction.EffectID,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);

	BindHalfByte(EffectStatePrediction.State,
		EGMC_PredictionMode::ClientAuth_Input,
		EGMC_CombineMode::CombineIfUnchanged,
		EGMC_SimulationMode::None,
		EGMC_InterpolationFunction::TargetValue);
	} 

void UGMC_AbilityComponent::GenAncillaryTick_Implementation(float DeltaTime, bool bIsCombinedClientMove)
{
	Super::GenAncillaryTick_Implementation(DeltaTime, bIsCombinedClientMove);
	TickActiveEffects(DeltaTime);
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
		
		Ability->Execute(this, InAbilityData);

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

	
	for (TSubclassOf<UGMCAbilityEffect> Effect : StartingEffects)
	{
		ApplyAbilityEffect(DuplicateObject(Effect->GetDefaultObject<UGMCAbilityEffect>(), this));
	}
	StartingEffects.Empty();
	
	bJustTeleported = false;
	// Effects
	if (EffectStatePrediction.EffectID != -1)
	{
		UpdateEffectState(EffectStatePrediction);
		// QueuedEffectStates.Push(EffectStatePrediction);
		EffectStatePrediction = {};
	}
	
	// Abilities
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
	InitializeEffectAssetData();
}


bool UGMC_AbilityComponent::CanAffordAbilityCost(UGMCAbility* Ability)
{
	UGMCAbilityEffect* AbilityEffect = Ability->AbilityCost->GetDefaultObject<UGMCAbilityEffect>();
	for (FGMCAttributeModifier AttributeModifier : AbilityEffect->Modifiers)
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

void UGMC_AbilityComponent::InitializeEffectAssetData()
{
	// Todo: Cache the UObjectLibrary stuff 
}

void UGMC_AbilityComponent::TickActiveEffects(float DeltaTime)
{
	for (FEffectStatePrediction State : QueuedEffectStates)
	{
		UpdateEffectState(State);
	}
	QueuedEffectStates.Empty();
	
	TArray<UGMCAbilityEffect*> CompletedPredictedEffects;
	TArray<int> CompletedActiveEffects;

	// Tick Effects
	for (UGMCAbilityEffect* Effect : PredictedActiveEffects)
	{
		Effect->Tick(DeltaTime);
	}
	for (const TPair<int, UGMCAbilityEffect*>& Effect : ActiveEffects)
	{
		Effect.Value->Tick(DeltaTime);
		if (Effect.Value->bCompleted) {CompletedActiveEffects.Push(Effect.Key);}
	}
	
	// Clean expired effects
	for (UGMCAbilityEffect* Effect : CompletedPredictedEffects)
	{
		PredictedActiveEffects.Remove(Effect);
	}
	for (const int EffectID : CompletedActiveEffects)
	{
		ActiveEffects.Remove(EffectID);
	}
}

void UGMC_AbilityComponent::UpdateEffectState(FEffectStatePrediction EffectState)
{
	if (!ActiveEffects.Contains(EffectState.EffectID))
	{
		UE_LOG(LogTemp, Error, TEXT("%d: Attempted to update invalid state ID: %d"), GetOwnerRole(), EffectState.EffectID);
		return;
	}

	ActiveEffects[EffectState.EffectID]->UpdateState(static_cast<EEffectState>(EffectState.State));
}

UGMCAbilityEffect* UGMC_AbilityComponent::GetMatchingPredictedEffect(UGMCAbilityEffect* InEffect)
{
	for (UGMCAbilityEffect* Effect : PredictedActiveEffects)
	{
		if (Effect->GetClass() == InEffect->GetClass())
		{
			UE_LOG(LogTemp, Warning, TEXT("Predicted Effect Found! (%s)"), *InEffect->GetClass()->GetName());
			return Effect;
		}
	}
	return nullptr;
}

void UGMC_AbilityComponent::SetAttributes(UGMCAttributeSet* NewAttributes)
{
	this->Attributes = NewAttributes;
}

void UGMC_AbilityComponent::ApplyAbilityCost(UGMCAbility* Ability)
{

	if (Ability->AbilityCost != nullptr)
	{
		ApplyAbilityEffect(DuplicateObject(Ability->AbilityCost->GetDefaultObject<UGMCAbilityEffect>(), this));
	}
}

UGMCAbilityEffect* UGMC_AbilityComponent::ApplyAbilityEffect(TSubclassOf<UGMCAbilityEffect> Effect, TArray<FGMCAttributeModifier> AdditionalModifiers, bool bOverwriteExistingModifiers, bool bAppliedByServer)
{
	if (Effect == nullptr) return nullptr;
	UGMCAbilityEffect* AbilityEffect = DuplicateObject(Effect->GetDefaultObject<UGMCAbilityEffect>(), this);
	FGMCAbilityEffectData EffectData = {};
	EffectData.Modifiers = AdditionalModifiers;
	EffectData.bOverwriteExistingModifiers = bOverwriteExistingModifiers;
	ApplyAbilityEffect(AbilityEffect, bAppliedByServer, EffectData);
	return AbilityEffect;
}

UGMCAbilityEffect* UGMC_AbilityComponent::ApplyAbilityEffect(UGMCAbilityEffect* Effect, bool bServerApplied, FGMCAbilityEffectData InitializationData)
{
	// Instant effects touch attributes, which are already replicated, so can just fire immediately
	// if (Effect->EffectType == EEffectType::Instant)
	// {
	// 	ApplyAbilityEffectModifiers(Effect);
	// 	return Effect;
	// }
	if (Effect == nullptr) return nullptr;
	// Duration effects
	Effect->InitializeEffect(this, bServerApplied, InitializationData);

	// Apply effect on server
	// Client needs to be told about effects that last for a duration
	// Ideally they've already predicted it
	if (GetOwner()->HasAuthority())
	{
		InitializationData.ServerStartTime = Effect->GetStartTime();
		InitializationData.ServerEndTime = Effect->GetEndTime();
		InitializationData.Duration = Effect->Duration;
		InitializationData.EffectID = GetNextEffectID();
		RPCServerApplyEffect(Effect->GetClass()->GetName(), InitializationData);
		ActiveEffects.Add(InitializationData.EffectID, Effect);
		return Effect;
	}

	// This is a client predicted event
	if (!bServerApplied)
	{
		PredictedActiveEffects.Push(Effect);
		UE_LOG(LogTemp, Warning, TEXT("Added Predicted Effect: %s"), *Effect->GetClass()->GetName())
		return Effect;
	}
	
	// Check if there is a predicted effect matching the incoming effect class 
	// If one isn't found, push to ActiveEffects array
	if (UGMCAbilityEffect* PredictedEffect = GetMatchingPredictedEffect(Effect))
	{
		PredictedActiveEffects.Remove(PredictedEffect);
		ActiveEffects.Add(InitializationData.EffectID, PredictedEffect);
		UE_LOG(LogTemp, Warning, TEXT("Moved Predicted Effect to Active Effects: [%d] %s"),InitializationData.EffectID,
		*Effect->GetClass()->GetName())
	}
	else
	{
		// Always push to active events
		ActiveEffects.Add(InitializationData.EffectID, Effect);
		UE_LOG(LogTemp, Warning, TEXT("Received An Unpredicted Effect:[%d] %s"),InitializationData.EffectID,
			*Effect->GetClass()->GetName())
	}

	return nullptr;
}

void UGMC_AbilityComponent::RemoveActiveAbilityEffect(UGMCAbilityEffect* Effect)
{
	if (Effect == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("Tried to remove null active ability effect"));
		return;
	}

	Effect->EndEffect();
}

void UGMC_AbilityComponent::ClientPredictEffectStateChange(int EffectID, EEffectState State)
{
	EffectStatePrediction.EffectID = EffectID;
	EffectStatePrediction.State = static_cast<uint8>(State);
}

void UGMC_AbilityComponent::RemoveActiveAbilityModifiers(UGMCAbilityEffect* Effect)
{
	// Iterate through each attribute modifier and apply the inverse of it
	for (FGMCAttributeModifier AttributeModifier : Effect->Modifiers)
	{
		if (const FAttribute* AffectedAttribute = Attributes->GetAttributeByName(AttributeModifier.AttributeName))
		{
			AffectedAttribute->Value -= AttributeModifier.Value;
		}
	}
}

void UGMC_AbilityComponent::ApplyAbilityEffectModifiers(UGMCAbilityEffect* Effect)
{
	// Iterate through each attribute modifier and apply it
	for (FGMCAttributeModifier AttributeModifier : Effect->Modifiers)
	{
		FAttribute CurrentValue = Attributes->GetAttributeValueByName(AttributeModifier.AttributeName);
		if (!HasAuthority())
		{
			UE_LOG(LogTemp, Warning, TEXT("Current Value: %f"), CurrentValue.Value);
		}
		Attributes->SetAttributeByName(AttributeModifier.AttributeName, CurrentValue.Value + AttributeModifier.Value);
	}
}

void UGMC_AbilityComponent::RPCServerApplyEffect_Implementation(const FString& EffectClassName, FGMCAbilityEffectData EffectInitializationData)
{
	// Todo: This can all be cached, not run every time
	// Also needs to support non BP effects
	// https://www.reddit.com/r/unrealengine/comments/ptxadm/is_it_possible_to_get_a_class_using/
	// FindObject<UClass>(ANY_PACKAGE, TEXT("MyClassName"));
	
	UObjectLibrary* Library = UObjectLibrary::CreateLibrary(UGMCAbilityEffect::StaticClass(), true, GIsEditor);
	Library->bRecursivePaths = true;
	Library->LoadBlueprintsFromPath("/Game");
	
	TArray<UBlueprintGeneratedClass *> ClassesArray;
	Library->GetObjects<UBlueprintGeneratedClass>(ClassesArray);

	for (int32 i = 0; i < ClassesArray.Num(); ++i)
	{
		UBlueprintGeneratedClass * BlueprintClass = ClassesArray[i];

		if (EffectClassName == BlueprintClass->GetName())
		{
			UGMCAbilityEffect* Effect = DuplicateObject(BlueprintClass->GetDefaultObject<UGMCAbilityEffect>(), this);
			ApplyAbilityEffect(Effect, true, EffectInitializationData);
			// UE_LOG(LogTemp, Warning, TEXT("[GMCABILITY] Successfully Found Effect Class From Server: %s"), *EffectClassName);
			return;
		}
	}
	UE_LOG(LogTemp, Error, TEXT("[GMCABILITY] Received Invalid Effect Class Name From Server: %s"), *EffectClassName);
}



