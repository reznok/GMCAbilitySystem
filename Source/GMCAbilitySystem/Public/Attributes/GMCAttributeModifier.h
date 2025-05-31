#pragma once
#include "GameplayTags.h"
#include "GMCAttributeModifierCustom_Base.h"
#include "GMCAttributeModifier.generated.h"


class UGMCAbilityEffect;
class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	// Adds to value
	Add = 4 UMETA(DisplayName = "+", AdvancedDisplay = "Adds to current value (Step 5)"),
	// Adds to value multiplier. Base Multiplier is 1. A modifier value of 1 will double the value.
	Multiply = 3 UMETA(DisplayName = "%", AdvancedDisplay = "Percentage of current value (0.1 = +10%) (Step 4)"),
	// Sets the value to the modifier value
	Set = 1 UMETA(DisplayName = "=", AdvancedDisplay = "Set the current value (Step 2)"),
	// Adds to the base value of the attribute
	AddMultiplyBaseValue = 2 UMETA(DisplayName = "⊕%", AdvancedDisplay = "Percentage of base value added to current value (0.1 = +10%) (Step 3)"),
	// Reset to the base value of the attribute
	SetToBaseValue = 0 UMETA(DisplayName = "⟲", AdvancedDisplay = "Reset current value to the base value (Step 1)"),
};

UENUM(BlueprintType)
enum class EGMCModifierPhase : uint8 {
	EMP_Pre UMETA(DisplayName = "Pre-Stack", ToolTip = "Will be applied before Stack to the value"),
	EMP_Stack UMETA(DisplayName = "Stack", ToolTip = "Operator of the same kind will be added together, then apply to the value"),
	EMP_Post UMETA(DisplayName = "Post-Stack", ToolTip = "Will be applied after Stack to the value"),
	EMP_Final UMETA(DisplayName = "Final", ToolTip = "At the very end"),
};

USTRUCT(BlueprintType)
struct FGMCAttributeModifier
{
	GENERATED_BODY()
		
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag AttributeTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta=(EditCondition = "CustomModifierClass == nullptr"))
	bool bValueIsAttribute{false};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute", EditCondition = "bValueIsAttribute && CustomModifierClass == nullptr", EditConditionHides))
	FGameplayTag ValueAsAttribute;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGMC_AbilitySystemComponent> SourceAbilitySystemComponent{nullptr};

	UPROPERTY(Transient)
	TWeakObjectPtr<UGMCAbilityEffect> SourceAbilityEffect{nullptr};

	UPROPERTY(Transient)
	bool bRegisterInHistory{false};

	// Value to modify the attribute by
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(EditCondition = "!bValueIsAttribute && CustomModifierClass == nullptr", EditConditionHides))
	float Value{0};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EModifierType Op{EModifierType::Add};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EGMCModifierPhase Phase {EGMCModifierPhase::EMP_Stack};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	TSubclassOf<UGMCAttributeModifierCustom_Base> CustomModifierClass{nullptr};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	int Priority{0};

	// Metadata tags to be passed with the attribute
	// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MetaTags;
	
};