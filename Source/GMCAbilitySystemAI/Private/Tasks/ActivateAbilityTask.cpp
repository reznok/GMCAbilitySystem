#include "Tasks/ActivateAbilityTask.h"

#include "GMCAbilitySystemAI.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FActivateAbilityTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Get the instance data
	FGMASActivateAbilityInstanceData& InstanceData = Context.GetInstanceData(*this);

	if (AbilityClass == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASActivateAbilityTask] No AbilityClass set"));
		return EStateTreeRunStatus::Failed;
	}
	
	InstanceData.AbilityInstance = InstanceData.AbilitySystemComponent->AI_ActivateAbilityByClass(AbilityClass);
	
	if (InstanceData.AbilityInstance == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASActivateAbilityTask] Failed to activate ability"));
		return EStateTreeRunStatus::Failed;
	}

	if (bWaitForAbilityEnd)
	{
		// Wait for the ability to end
		return EStateTreeRunStatus::Running;
	}
	return EStateTreeRunStatus::Succeeded;
}

void FActivateAbilityTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	if (bEndAbilityAtStateEnd)
	{
		const FGMASActivateAbilityInstanceData& InstanceData = Context.GetInstanceData(*this);
		if (InstanceData.AbilityInstance != nullptr)
		{
			InstanceData.AbilityInstance->EndAbility();
		}
	}
	
	FStateTreeGMASTaskBase::ExitState(Context, Transition);
}

EStateTreeRunStatus FActivateAbilityTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime) const
{
	const FGMASActivateAbilityInstanceData& InstanceData = Context.GetInstanceData(*this);
	if (InstanceData.AbilityInstance == nullptr)
	{
		UE_LOG(LogGMCAbilitySystemAI, Warning, TEXT("[StateTreeGMASActivateAbilityTask] No AbilityInstance set in Tick"));
		return EStateTreeRunStatus::Failed;
	}
	
	if (InstanceData.AbilityInstance->AbilityState == EAbilityState::Ended)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Running;
}
