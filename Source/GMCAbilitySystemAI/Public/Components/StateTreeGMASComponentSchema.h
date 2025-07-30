// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GMCAbilityComponent.h"
#include "Components/StateTreeComponentSchema.h"
#include "StateTreeGMASComponentSchema.generated.h"

class AAIController;
/**
 * 
 */
UCLASS(BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "StateTree GMAS Component"))
class GMCABILITYSYSTEMAI_API UStateTreeGMASComponentSchema : public UStateTreeComponentSchema
{
	GENERATED_BODY()
	
public:
	UStateTreeGMASComponentSchema(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	virtual void PostLoad() override;

	virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;

	static bool SetContextRequirements(UBrainComponent& BrainComponent, FStateTreeExecutionContext& Context, bool bLogErrors = false);

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

protected:
	
	/** AbilitySystemComponent class the StateTree is expected to run on. Allows to bind to specific Actor class' properties. */
	UPROPERTY(EditAnywhere, Category = "Defaults", NoClear)
	TSubclassOf<UGMC_AbilitySystemComponent> AbilitySystemComponentClass = nullptr;
};
