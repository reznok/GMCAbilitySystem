#pragma once
#include "GameplayTags.h"
#include "GMCAttributeModifierCustom_Base.h"
#include "GMCAttributeModifier.generated.h"


class UGMCAbilityEffect;
class UGMC_AbilitySystemComponent;

UENUM(BlueprintType)
enum class EModifierType : uint8
{
	Add UMETA(DisplayName = "+ [Add]"),
	// Add To Attribute a Percentage of the Base Value
	AddPercentageInitialValue UMETA(DisplayName = "% [Add Percentage Of Initial Value]"),
	// Add to Attribute the Percentage of an selected Attribute (Value is the Percentage, Attribute as Value is the Attribute to use)
	AddPercentageAttribute UMETA(DisplayName = "% [Add Percentage Of Attribute]"),
	// Add to Attribute the Percentage of the Max Clamp Value
	AddPercentageMaxClamp UMETA(DisplayName = "% [Add Percentage Of Max Clamp]"),
	// Add to Attribute the Percentage of the Min Clamp Value
	AddPercentageMinClamp UMETA(DisplayName = "% [Add Percentage Of Min Clamp]"),
	// Add to Attribute the Percentage of the sum of selecteds Attributes (+20% of the Intelligence + Strength)
	AddPercentageAttributeSum UMETA(DisplayName = "% [Add Percentage Of Attribute Sum]"),
	// Add to Attribute a Value Scaled between X and Y depending of Value
	AddScaledBetween UMETA(DisplayName = "X-y [Scaled Between]"),
	// Add to Attribute a value, but this is Clamped between X and Y
	AddClampedBetween UMETA(DisplayName = "+ [Add Clamped]"),
	// Add to Attribute the Percentage of the Missing Value compare to the Base Value
	AddPercentageMissing UMETA(DisplayName = "% [Add Percentage Of Missing Value]"),
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

		// Return the value (Auto apply Custom Modifier if set, or return target attribute, or raw value)
		float GetValue() const;

		// Return the value to apply to an attribute on calculation.
		float CalculateModifierValue(const FAttribute& Attribute) const;

		// If isn't ticking, set DeltaTime to 1.f !
		void InitModifier(UGMCAbilityEffect* Effect, double InActionTimer, int InApplicationIdx, bool bInRegisterInHistory = false, float
		                  InDeltaTime = 1.f);
		
		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
		FGameplayTag AttributeTag;

		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta=(DisplayAfter = "AttributeTag"))
		EGMCAttributeModifierType ValueType {EGMCAttributeModifierType::AMT_Value};

		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute", EditConditionHides,
			EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Attribute || Op == EModifierType::AddPercentageAttribute",
			DisplayAfter = "ValueType"))
		FGameplayTag ValueAsAttribute;
	
		UPROPERTY(Transient)
		TWeakObjectPtr<UGMCAbilityEffect> SourceAbilityEffect{nullptr};

		UPROPERTY(Transient)
		bool bRegisterInHistory{false};

		float DeltaTime {1.f};

		double ActionTimer {0.0};

		int ApplicationIndex{0};

		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
		EModifierType Op{EModifierType::Add};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(DisplayAfter = "ValueType", EditConditionHides, EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Custom"))
		TSubclassOf<UGMCAttributeModifierCustom_Base> CustomModifierClass{nullptr};
	
		// Metadata tags to be passed with the attribute
		// Ie: DamageType (Element.Fire, Element.Electric), DamageSource (Source.Player, Source.Boss), etc
		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem")
		FGameplayTagContainer MetaTags;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Value", EditConditionHides, DisplayAfter = "ValueType"))
		float ModifierValue{0};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "(Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween) && !XAsAttribute", EditConditionHides
			, DisplayAfter = "XAsAttribute"))
		float X {0.f};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "(Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween) && !YAsAttribute", EditConditionHides
			, DisplayAfter = "YAsAttribute"))
		float Y {0.f};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", DisplayName="X As Attribute", 
		meta=(EditCondition = "Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween", EditConditionHides));
		bool XAsAttribute{false};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem", DisplayName="Y As Attribute",
		meta=(EditCondition = "Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween", EditConditionHides));
		bool YAsAttribute{false};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "(Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween) && XAsAttribute", EditConditionHides,
			DisplayAfter = "XAsAttribute"))
		FGameplayTag XAttribute;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "(Op == EModifierType::AddScaledBetween || Op == EModifierType::AddClampedBetween) && YAsAttribute", EditConditionHides,
			DisplayAfter = "YAsAttribute"))
		FGameplayTag YAttribute;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem",
		meta=(EditCondition = "Op == EModifierType::AddPercentageAttributeSum", EditConditionHides, DisplayAfter = "ValueType"))
		FGameplayTagContainer Attributes;
	
};