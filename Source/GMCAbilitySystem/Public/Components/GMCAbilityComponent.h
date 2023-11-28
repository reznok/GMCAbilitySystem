// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMCAbility.h"
#include "GMCMovementUtilityComponent.h"
#include "Components/ActorComponent.h"
#include "GMCAbilityComponent.generated.h"


USTRUCT(BlueprintType)
struct FGMCAbilityAttributes
{
	GENERATED_BODY()

	FGMCAbilityAttributes()
	{
		Health = 0;
		MaxHealth = 0;
		Stamina = 0;
		MaxStamina = 0;
	}

	// Attributes
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float Health = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float MaxHealth = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float Stamina = 100;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attributes")
	float MaxStamina = 100;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class GMCABILITYSYSTEM_API UGMC_AbilityComponent : public UGMC_MovementUtilityCmp
{
	GENERATED_BODY()

public:
	// Sets default values for this component's properties
	UGMC_AbilityComponent();

	bool TryActivateAbility(FGMCAbilityData AbilityData);

	// Queue an ability to be executed
	void QueueAbility(FGMCAbilityData AbilityData);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGMCAbilityAttributes Attributes;

	UFUNCTION(BlueprintCallable)
	void ApplyAbilityCost(UGMCAbility* Ability);

protected:
	virtual void BindReplicationData_Implementation() override;
	virtual void GenPredictionTick_Implementation(float DeltaTime) override;
	virtual void PreLocalMoveExecution_Implementation(const FGMC_Move& LocalMove) override;

	bool CanAffordAbilityCost(UGMCAbility* Ability);
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Abilities")
	TArray<TSubclassOf<UGMCAbility>> GrantedAbilities;

private:
	TArray<FGMCAbilityData> QueuedAbilities;

	// Current Ability Data being processed
	// Members of this struct are bound over GMC
	FGMCAbilityData AbilityData{};
	
};
