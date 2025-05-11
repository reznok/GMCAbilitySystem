// Fill out your copyright notice in the Description page of Project Settings.


#include "Conditions/HasActiveTag.h"
#include "GMCAbilityComponent.h"
#include "StateTreeExecutionContext.h"

bool FGMASHasActiveTagCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	if (bExactMatch)
	{
		return InstanceData.AbilitySystemComponent->HasActiveTagExact(InstanceData.TagToCheck) ^ bInvert;
	}
	return InstanceData.AbilitySystemComponent->HasActiveTag(InstanceData.TagToCheck) ^ bInvert;
}
