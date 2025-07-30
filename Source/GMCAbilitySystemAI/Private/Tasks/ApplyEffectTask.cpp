#include "Tasks/ApplyEffectTask.h"

#include "GMCAbilitySystemAI.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FApplyEffectTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Get the instance data
	FGMASApplyEffectInstanceData& InstanceData = Context.GetInstanceData(*this);

	if (EffectClass == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASApplyEffectTask] No EffectClass set"));
		return EStateTreeRunStatus::Failed;
	}

	// Apply Effect
	InstanceData.EffectInstance = InstanceData.AbilitySystemComponent->ApplyAbilityEffect(EffectClass, EffectData, false);
	
	if (InstanceData.EffectInstance == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASActivateAbilityTask] Failed to activate ability"));
		return EStateTreeRunStatus::Failed;
	}

	if (bWaitForEffectEnd)
	{
		// Wait for the ability to end
		return EStateTreeRunStatus::Running;
	}
	return EStateTreeRunStatus::Succeeded;
}

void FApplyEffectTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (bEndEffectAtStateEnd)
	{
		const FGMASApplyEffectInstanceData& InstanceData = Context.GetInstanceData(*this);
		if (InstanceData.EffectInstance != nullptr)
		{
			InstanceData.EffectInstance->EndEffect();
		}
	}
	FStateTreeGMASTaskBase::ExitState(Context, Transition);
}

EStateTreeRunStatus FApplyEffectTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FGMASApplyEffectInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.EffectInstance == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASActivateAbilityTask] No AbilityInstance set in Tick"));
		return EStateTreeRunStatus::Failed;
	}
	
	if (InstanceData.EffectInstance->CurrentState == EGMASEffectState::Ended)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}
