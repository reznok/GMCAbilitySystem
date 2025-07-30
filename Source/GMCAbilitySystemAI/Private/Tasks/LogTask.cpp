#include "Tasks/LogTask.h"

#include "GMCAbilitySystemAI.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus FLogTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const
{
	// Get the instance data
	const FGMASLogInstanceData& InstanceData = Context.GetInstanceData(*this);

	// Log the message
	UE_LOG(LogGMCAbilitySystemAI, Log, TEXT("[StateTreeGMASLogTask] %s"), *LogMessage);

	return EStateTreeRunStatus::Succeeded;
}