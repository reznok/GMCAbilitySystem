#pragma once
#include "GameplayTags.h"
#include "GMCAttributeModifierCustom_Base.h"
#include "GMCAttributeModifier.generated.h"


class UGMCAbilityEffect;
class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	Add = 7 UMETA(DisplayName = "+ [Add]", ToolTip = "Adds to current value (Step 7)"),
	Percentage = 3 UMETA(DisplayName = "% [Percentage]", ToolTip = "Set Current value to a Percentage of Current value (10 = 10%) (Step 4)"),
	Set = 1 UMETA(DisplayName = "= [Set]", ToolTip = "Set current value (Step 2)"),
	AddPercentageBaseValue = 2 UMETA(DisplayName = "% [Add Percentage BaseValue]", ToolTip = "Percentage of base value added to current value (10 = +10%) (Step 3)"),
	AddPercentage = 4 UMETA(DisplayName = "% [Add Percentage]", ToolTip = "Percentage of value added to current value (10 = +10%) (Step 5)"),
	PercentageBaseValue = 5 UMETA(DisplayName = "% [Percentage BaseValue]", ToolTip = "Set Current Value to a Percentage of Base Value (10 = 10%) (Step 6)"),
	SetToBaseValue = 0 UMETA(DisplayName = "= [Set To BaseValue]", ToolTip = "Reset current value to the base value (Step 1)"),
};

UENUM(BlueprintType)
enum class EGMCModifierPhase : uint8 {
	EMP_Pre UMETA(DisplayName = "Pre-Stack", ToolTip = "Will be applied before Stack to the value"),
	EMP_Stack UMETA(DisplayName = "Stack", ToolTip = "Operator of the same kind will be added together, then apply to the value"),
	EMP_Post UMETA(DisplayName = "Post-Stack", ToolTip = "Will be applied after Stack to the value"),
	EMP_Final UMETA(DisplayName = "Final", ToolTip = "At the very end"),
};

UENUM(BlueprintType)
enum class EGMCAttributeModifierType : uint8
{
	AMT_Value UMETA(DisplayName = "Value", ToolTip = "Raw Value"),
	AMT_Attribute UMETA(DisplayName = "Attribute", ToolTip = "Attribute that will be used to calculate the value"),
	AMT_Custom UMETA(DisplayName = "Custom", ToolTip = "Custom modifier class that will be used to calculate the value"),
};

USTRUCT(BlueprintType)
struct FGMCAttributeModifier
{
	GENERATED_BODY()

	public:
	
	float GetModifierValue(const FAttribute* LinkedAttribute, float DeltaTime = -1.f) const;

	// This is the final value of the attribute, only valid after ProcessValue is called
	float GetRegisteredValue() const
	{
		return CurrentModifierValue;
	}

	void RegisterModifierValue(const FAttribute* LinkedAttribute, float DeltaTime = -1.f)
	{
		CurrentModifierValue = GetModifierValue(LinkedAttribute, DeltaTime);
	}
		
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
	FGameplayTag AttributeTag;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta=(DisplayAfter = "AttributeTag"))
	EGMCAttributeModifierType ValueType {EGMCAttributeModifierType::AMT_Value};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute", EditConditionHides, EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Attribute", DisplayAfter = "ValueType"))
	FGameplayTag ValueAsAttribute;

	UPROPERTY(Transient)
	TWeakObjectPtr<UGMC_AbilitySystemComponent> SourceAbilitySystemComponent{nullptr};

	UPROPERTY(Transient)
	TWeakObjectPtr<UGMCAbilityEffect> SourceAbilityEffect{nullptr};

	UPROPERTY(Transient)
	bool bRegisterInHistory{false};



	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	EModifierType Op{EModifierType::Add};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	EGMCModifierPhase Phase {EGMCModifierPhase::EMP_Stack};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(DisplayAfter = "ValueType", EditConditionHides, EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Custom"))
	TSubclassOf<UGMCAttributeModifierCustom_Base> CustomModifierClass{nullptr};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", AdvancedDisplay)
	int Priority{0};

	// Metadata tags to be passed with the attribute
	// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
	FGameplayTagContainer MetaTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", meta=(EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Value", EditConditionHides, DisplayAfter = "ValueType"))
	float ModifierValue{0};

	protected:
	
	// Final value processed
	float CurrentModifierValue{0};
	
};