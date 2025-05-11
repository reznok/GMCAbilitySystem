#include "Tasks/StateTreeGMASLogTask.h"

#include "GMCAbilitySystemAI.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FStateTreeGMASLogTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Get the instance data
	const FGMASLogInstanceData& InstanceData = Context.GetInstanceData(*this);

	// Log the message
	UE_LOG(LogGMCAbilitySystemAI, Log, TEXT("[StateTreeGMASLogTask] %s"), *LogMessage);

	return EStateTreeRunStatus::Succeeded;
}