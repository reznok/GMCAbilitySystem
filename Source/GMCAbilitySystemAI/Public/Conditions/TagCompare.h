// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "TagCompare.generated.h"


class UGMC_AbilitySystemComponent;
class UStateTreeConditionBlueprintBase;

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASCompareTagConditionInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag LeftTag;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag RightTag;
};

/**
 * Tag Compare condition
 * Succeeds if the Left Tag matches the Right Tag
 *
 * if bMatchesExact is true, the Left Tag must match the Right Tag exactly
 * if bMatchesExact is false, the Left Tag must match the Right Tag or any of its parents
 */

USTRUCT(DisplayName="Tag Compare")
struct GMCABILITYSYSTEMAI_API FGMASTagCompareCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASCompareTagConditionInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FGMASTagCompareCondition() = default;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

#if WITH_EDITOR
	// virtual FText GetDescription(const FGuid& ID, FStateTreeDataView InstanceDataView, const IStateTreeBindingLookup& BindingLookup, EStateTreeNodeFormatting Formatting = EStateTreeNodeFormatting::Text) const override;
	virtual FName GetIconName() const override
	{
		return FName("StateTreeEditorStyle|Node.Tag");
	}
	virtual FColor GetIconColor() const override
	{
		return UE::StateTree::Colors::DarkGrey;
	}
#endif
	
	/** If true, the tag has to be exactly present, if false then TagContainer will include its parent tags while matching */
	UPROPERTY(EditAnywhere, Category = Condition)
	bool bExactMatch = true;
};
