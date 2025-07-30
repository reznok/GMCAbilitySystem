#pragma once
#include "StateTreeGMASTask.h"
#include "Components/StateTreeComponent.h"
#include "Ability/GMCAbility.h"
#include "ActivateAbilityTask.generated.h"

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASApplyEffectInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Context")
	UGMC_AbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY()
	TWeakObjectPtr<UGMCAbilityEffect> EffectInstance = nullptr;
};

USTRUCT(meta = (DisplayName = "Apply Effect", Category = "GMAS"))
struct GMCABILITYSYSTEMAI_API FApplyEffectTask : public FStateTreeGMASTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASApplyEffectInstanceData;

	FApplyEffectTask() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;


	UPROPERTY(EditAnywhere, Category = "Parameter")
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	// Apply custom InitializationData to the effect
	// Just like in Abilities, this is only needed if the effect needs custom initialization data
	UPROPERTY(EditAnywhere, Category = "Parameter")
	FGMCAbilityEffectData EffectData;

	// If true, the task will wait for the effect to end before succeeding
	// If false, the task will succeed immediately after successfully applying the effect
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bWaitForEffectEnd = true;

	// If true, the task will end the effect when the state ends
	// If false, the effect will remain active even after the state ends
	// Useful for removing effects when a state is prematurely exited
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bEndEffectAtStateEnd = true;
};
