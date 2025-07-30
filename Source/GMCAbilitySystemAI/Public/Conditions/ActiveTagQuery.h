// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "ActiveTagQuery.generated.h"


class UGMC_AbilitySystemComponent;
class UStateTreeConditionBlueprintBase;

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASActiveTagQueryInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	UGMC_AbilitySystemComponent* AbilitySystemComponent;
};

/**
 * HasActiveTag condition
 * Succeeds if the ASC has the specified tag.
 * 
 * Condition can be used with multiple configurations:
 *	Does TagContainer {"A.1"} has Tag "A" ?
 *		exact match 'false' will SUCCEED
 *		exact match 'true' will FAIL
 */

USTRUCT(DisplayName="Has Active Tag", Category="GMAS")
struct GMCABILITYSYSTEMAI_API FGMASActiveTagQueryCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASActiveTagQueryInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FGMASActiveTagQueryCondition() = default;

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

	UPROPERTY(EditAnywhere, Category = Condition)
	FGameplayTagQuery TagQuery;
};
