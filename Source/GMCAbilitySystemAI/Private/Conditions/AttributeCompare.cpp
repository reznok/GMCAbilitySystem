// Fill out your copyright notice in the Description page of Project Settings.


#include "Conditions/AttributeCompare.h"
#include "GMCAbilityComponent.h"
#include "StateTreeExecutionContext.h"

bool FGMASAttributeCompareCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const FInstanceDataType& InstanceData = Context.GetInstanceData(*this);

	// Get the left attribute value
	const float LeftValue = InstanceData.AbilitySystemComponent->GetAttributeValueByTag(LeftAttribute);

	// Get the right attribute value
	float RightComparisonValue = RightValue;

	if (RightAttribute.IsValid())
	{
		RightComparisonValue = InstanceData.AbilitySystemComponent->GetAttributeValueByTag(RightAttribute);
	}
	RightComparisonValue *= PercentageModifier;

	switch (Operator)
	{
		case EGenericAICheck::Equal:
			return LeftValue == RightComparisonValue;
		case EGenericAICheck::NotEqual:
			return LeftValue != RightComparisonValue;
		case EGenericAICheck::Less:
			return LeftValue < RightComparisonValue;
		case EGenericAICheck::LessOrEqual:
			return LeftValue <= RightComparisonValue;
		case EGenericAICheck::Greater:
			return LeftValue > RightComparisonValue;
		case EGenericAICheck::GreaterOrEqual:
			return LeftValue >= RightComparisonValue;
		default:
			ensureMsgf(false, TEXT("Unhandled operator %d"), Operator);
			return false;
	}
}
