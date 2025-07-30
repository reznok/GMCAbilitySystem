// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AITypes.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "AttributeCompare.generated.h"


enum class EGenericAICheck : uint8;
class UGMC_AbilitySystemComponent;
class UStateTreeConditionBlueprintBase;

UENUM(Meta = (Bitflags))
enum class EAttributeComparisonType : uint8
{
	Value,
	Attribute
};


USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASCompareAttributeConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	UGMC_AbilitySystemComponent* AbilitySystemComponent;
};

/**
 * Compare Attribute values
 */

USTRUCT(DisplayName="Attribute Compare", Category="GMAS")
struct GMCABILITYSYSTEMAI_API FGMASAttributeCompareCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASCompareAttributeConditionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FGMASAttributeCompareCondition() = default;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	// virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual FName GetIconName() const override
	{
		return FName("StateTreeEditorStyle");
	}
	virtual FColor GetIconColor() const override
	{
		return UE::StateTree::Colors::DarkGrey;
	}
#endif

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bitmask, BitmaskEnum = "EAttributeComparisonType"))
	EAttributeComparisonType ComparisonType = EAttributeComparisonType::Value;
	
	UPROPERTY(EditAnywhere, Category = "Parameter", meta = (InvalidEnumValues = "IsTrue"))
	EGenericAICheck Operator = EGenericAICheck::Equal;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta=(Categories="Attribute"))
	FGameplayTag LeftAttribute;

	UPROPERTY(EditAnywhere, Category = "Parameter", meta=(EditCondition = "ComparisonType == EAttributeComparisonType::Value"))
	float RightValue;
	
	UPROPERTY(EditAnywhere, Category = "Parameter", meta=(Categories="Attribute", EditCondition = "ComparisonType == EAttributeComparisonType::Attribute"))
	FGameplayTag RightAttribute;
	
	// Multiply the RightValue by this percentage before comparing
	// Useful for checking if an attribute is below a certain percentage of another attribute
	// Ex: if Health is below 50% of MaxHealth
	UPROPERTY(EditAnywhere, Category = "Parameter")
	float PercentageModifier = 1.0f;
};
