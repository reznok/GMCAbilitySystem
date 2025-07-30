// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "HasActiveEffect.generated.h"


class UGMCAbilityEffect;
class UGMC_AbilitySystemComponent;
class UStateTreeConditionBlueprintBase;

UENUM(Meta = (Bitflags))
enum class EEffectComparisonType : uint8
{
	TagQuery,
	EffectClass,
};

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASHasActiveEffectInstanceData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = Context)
	UGMC_AbilitySystemComponent* AbilitySystemComponent;
};

/**
 * Check if the ability system has an active effect that matches a FGameplayTagQuery or class
 */

USTRUCT(DisplayName="Has Active Effect", Category="GMAS")
struct GMCABILITYSYSTEMAI_API FGMASHasActiveEffectCondition : public FStateTreeConditionCommonBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASHasActiveEffectInstanceData;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	FGMASHasActiveEffectCondition() = default;

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

	UPROPERTY(EditAnywhere, Category = Condition, meta = (Bitmask, BitmaskEnum = "EEffectComparisonType"))
	EEffectComparisonType ComparisonType = EEffectComparisonType::TagQuery;

	// Query to be matched
	UPROPERTY(EditAnywhere, Category = Parameter, meta = (EditCondition = "ComparisonType == EEffectComparisonType::TagQuery"))
	FGameplayTagQuery EffectQueryToCheck;

	UPROPERTY(EditAnywhere, Category = Parameter, meta = (EditCondition = "ComparisonType == EEffectComparisonType::EffectClass"))
	TSubclassOf<UGMCAbilityEffect> EffectClassToCheck;

	UPROPERTY(EditAnywhere, Category = Condition)
	bool bInvert = false;
};
