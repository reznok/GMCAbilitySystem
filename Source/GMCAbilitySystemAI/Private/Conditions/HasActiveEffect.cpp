// Fill out your copyright notice in the Description page of Project Settings.


#include "Conditions/HasActiveEffect.h"
#include "GMCAbilityComponent.h"
#include "StateTreeExecutionContext.h"

bool FGMASHasActiveEffectCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	switch (ComparisonType)
	{
	case EEffectComparisonType::TagQuery:
		return InstanceData.AbilitySystemComponent->QueryActiveEffects(EffectQueryToCheck) ^ bInvert;
	case EEffectComparisonType::EffectClass:
		return InstanceData.AbilitySystemComponent->HasActiveEffectWithClass(EffectClassToCheck) ^ bInvert;
	default:
		return false;
	}
	
}
