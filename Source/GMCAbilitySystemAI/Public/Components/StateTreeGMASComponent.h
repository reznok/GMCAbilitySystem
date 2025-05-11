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

	virtual bool SetContextRequirements(FStateTreeExecutionContext& Context, bool bLogErrors = false) override;

	UFUNCTION()
	void OnActiveTagsChanged(const FGameplayTagContainer& AddedTags, const FGameplayTagContainer& RemovedTags);
	
	void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UGMC_AbilitySystemComponent> AbilitySystemComponent;
		
};
