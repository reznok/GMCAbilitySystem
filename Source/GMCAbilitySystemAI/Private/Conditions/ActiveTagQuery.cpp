// Fill out your copyright notice in the Description page of Project Settings.


#include "Conditions/ActiveTagQuery.h"
#include "GMCAbilityComponent.h"
#include "StateTreeExecutionContext.h"

bool FGMASActiveTagQueryCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);
	
	return InstanceData.AbilitySystemComponent->GetActiveTags().MatchesQuery(TagQuery);
}
