// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "HasActiveTag.generated.h"


class UGMC_AbilitySystemComponent;
class UStateTreeConditionBlueprintBase;

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASHasActiveTagInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	UGMC_AbilitySystemComponent* AbilitySystemComponent;
	
	UPROPERTY(EditAnywhere, Category = Parameter)
	FGameplayTag TagToCheck;
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
struct GMCABILITYSYSTEMAI_API FGMASHasActiveTagCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASHasActiveTagInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FGMASHasActiveTagCondition() = default;

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

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};
