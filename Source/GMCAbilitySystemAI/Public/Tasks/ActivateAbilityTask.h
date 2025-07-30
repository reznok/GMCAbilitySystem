#pragma once
#include "StateTreeGMASTask.h"
#include "Components/StateTreeComponent.h"
#include "Ability/GMCAbility.h"
#include "ActivateAbilityTask.generated.h"

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASActivateAbilityInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="Context")
	UGMC_AbilitySystemComponent* AbilitySystemComponent;

	UPROPERTY()
	TWeakObjectPtr<UGMCAbility> AbilityInstance = nullptr;
};

USTRUCT(meta = (DisplayName = "Activate Ability", Category = "GMAS"))
struct GMCABILITYSYSTEMAI_API FActivateAbilityTask : public FStateTreeGMASTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASActivateAbilityInstanceData;

	FActivateAbilityTask() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const override;


	UPROPERTY(EditAnywhere, Category = "Parameter")
	TSubclassOf<UGMCAbility> AbilityClass;

	// If true, the task will wait for the ability to end before succeeding
	// If false, the task will succeed immediately after successfully activating the ability
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bWaitForAbilityEnd = true;

	// If true, the task will end the ability when the state ends
	// If false, the ability will remain active even after the state ends
	// Requires the ability to properly handle clean up in the EndAbility event
	UPROPERTY(EditAnywhere, Category = "Parameter")
	bool bEndAbilityAtStateEnd = true;
};
