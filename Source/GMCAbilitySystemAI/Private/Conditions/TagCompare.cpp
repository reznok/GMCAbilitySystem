// Fill out your copyright notice in the Description page of Project Settings.


#include "Conditions/TagCompare.h"
#include "GMCAbilityComponent.h"
#include "StateTreeExecutionContext.h"

bool FGMASTagCompareCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (bExactMatch)
	{
		return InstanceData.LeftTag.MatchesTagExact(InstanceData.RightTag);
	}
	
	return InstanceData.LeftTag.MatchesTag(InstanceData.RightTag);
}
