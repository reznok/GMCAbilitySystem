// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "StateTreeGMASComponentSchema.h"
#include "StateTreeGMASAIComponentSchema.generated.h"


class AAIController;

/**
 * 
 */
UCLASS(MinimalAPI, BlueprintType, EditInlineNew, CollapseCategories, meta = (DisplayName = "GMAS AI Component", CommonSchema))
class UStateTreeGMASAIComponentSchema : public UStateTreeGMASComponentSchema
{
	GENERATED_BODY()

public:
	GMCABILITYSYSTEMAI_API UStateTreeGMASAIComponentSchema(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	
	GMCABILITYSYSTEMAI_API virtual void PostLoad() override;

	GMCABILITYSYSTEMAI_API virtual bool IsStructAllowed(const UScriptStruct* InScriptStruct) const override;

	static bool SetContextRequirements(UBrainComponent& BrainComponent, FStateTreeExecutionContext& Context, bool bLogErrors = false);
#if WITH_EDITOR
	GMCABILITYSYSTEMAI_API virtual void PostEditChangeChainProperty(FPropertyChangedChainEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR
	
protected:
	/** AIController class the StateTree is expected to run on. Allows to bind to specific Actor class' properties. */
	UPROPERTY(EditAnywhere, Category = "Defaults", NoClear)
	TSubclassOf<AAIController> AIControllerClass = nullptr;
};
