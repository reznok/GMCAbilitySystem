// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityComponent.h"
#include "UObject/Object.h"
#include "Components/StateTreeComponent.h"
#include "StateTreeGMASComponent.generated.h"


/**
 * 
 */
UCLASS(ClassGroup = AI, meta = (BlueprintSpawnableComponent))
class GMCABILITYSYSTEMAI_API UStateTreeGMASComponent : public UStateTreeComponent
{
	GENERATED_BODY()
	
public:
	//~ BEGIN IStateTreeSchemaProvider
	TSubclassOf<UStateTreeSchema> GetSchema() const override;
	//~ END

public:

	UFUNCTION()
	void OnActiveTagsChanged(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags);

	UFUNCTION()
	void OnEffectApplied(UGMCAbilityEffect* AppliedEffect);

	UFUNCTION()
	void OnEffectRemoved(UGMCAbilityEffect* RemovedEffect);
	
	UFUNCTION()
	void OnAbilityActivated(UGMCAbility* Ability, FGameplayTag AbilityTag);
	
	UFUNCTION()
	void OnAbilityEnded(UGMCAbility* Ability);

	UFUNCTION()
	void OnAttributeChanged(FGameplayTag AttributeTag, float OldValue, float NewValue);

	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;
		
};
