#pragma once
#include "GameplayTags.h"
#include "Templates/SubclassOf.h"
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
	// Add to Attribute the Percentage of an Attribute Raw Value (Raw Value is the attribute value without any temporal modifiers)
	AddPercentageOfAttributeRawValue UMETA(DisplayName = "% [Add Percentage Of Attribute Raw Value"),
	// Set the attribute to an absolute value. Layered: any active Add modifiers (past or future) keep stacking
	// on top of the Set base. The most recent Set wins (tie-broken by ApplicationIndex). Replay-safe via the
	// existing PurgeTemporalModifier path: Set entries are time-tagged and purged on rollback like any other.
	Set UMETA(DisplayName = "= [Set] (base value, prior Adds keep stacking)"),
	// Set the attribute to an absolute value AND ignore any Add modifiers placed before this Set's ActionTimer.
	// Adds placed AFTER the Set still stack on top. Use for "reset state" semantics (revive, mode override).
	SetReplace UMETA(DisplayName = "= [Set Replace] (clears prior Add modifiers)"),
};

UENUM(BlueprintType)
enum class EGMCAttributeModifierType : uint8
{
	AMT_Value UMETA(DisplayName = "Value", ToolTip = "Raw Value"),
	AMT_Attribute UMETA(DisplayName = "Attribute", ToolTip = "Attribute that will be used to calculate the value"),
	AMT_Custom UMETA(DisplayName = "Custom", ToolTip = "Custom modifier class that will be used to calculate the value"),
};

UENUM(BlueprintType)
enum class EGMCModifierConditionAction : uint8
{
	// Don't apply the modifier for this application (this tick / this instant).
	Skip          UMETA(DisplayName = "Skip (don't apply)"),
	// Replace the value SOURCE (Value / Attribute / Custom MMC) for this application.
	OverrideValue UMETA(DisplayName = "Override value source"),
};

// A single tag-driven rule attached to a modifier. Evaluated in array order at application
// time; the first rule whose Condition matches the owner's bound active tags wins.
USTRUCT(BlueprintType)
struct FGMCModifierCondition
{
	GENERATED_BODY()

	// Evaluated against the owner's BOUND active tags (replay-safe). An empty query never
	// matches (a conditionless rule is a config error; "always apply" is already the default).
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition")
	FGameplayTagQuery Condition;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition")
	EGMCModifierConditionAction Action {EGMCModifierConditionAction::Skip};

	// ---- Payload for OverrideValue (mirrors the modifier's own value-source fields) ----
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition",
		meta=(EditCondition = "Action == EGMCModifierConditionAction::OverrideValue", EditConditionHides))
	EGMCAttributeModifierType ValueType {EGMCAttributeModifierType::AMT_Value};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition",
		meta=(EditCondition = "Action == EGMCModifierConditionAction::OverrideValue && ValueType == EGMCAttributeModifierType::AMT_Value", EditConditionHides))
	float ModifierValue {0.f};

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition", meta=(Categories="Attribute",
		EditCondition = "Action == EGMCModifierConditionAction::OverrideValue && ValueType == EGMCAttributeModifierType::AMT_Attribute", EditConditionHides))
	FGameplayTag ValueAsAttribute;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Condition",
		meta=(EditCondition = "Action == EGMCModifierConditionAction::OverrideValue && ValueType == EGMCAttributeModifierType::AMT_Custom", EditConditionHides))
	TSubclassOf<UGMCAttributeModifierCustom_Base> CustomModifierClass {nullptr};
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

		// Returns false when the modifier must be SKIPPED for this application. On a matching
		// OverrideValue rule, mutates *this in place (value source only). Call AFTER InitModifier.
		bool ResolveConditions(const UGMC_AbilitySystemComponent* ASC);

		// If isn't ticking, set DeltaTime to 1.f !
		void InitModifier(UGMCAbilityEffect* Effect, double InActionTimer, int InApplicationIdx, bool bInRegisterInHistory = false, float
		                  InDeltaTime = 1.f);
		
		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute"))
		FGameplayTag AttributeTag;

		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta=(DisplayAfter = "AttributeTag"))
		EGMCAttributeModifierType ValueType {EGMCAttributeModifierType::AMT_Value};

		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category="Attribute", meta = (Categories="Attribute", EditConditionHides,
			EditCondition = "ValueType == EGMCAttributeModifierType::AMT_Attribute || Op == EModifierType::AddPercentageAttribute || Op == EModifierType::AddPercentageOfAttributeRawValue",
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

		// Ordered conditional rules. Evaluated at application time against the owner's bound active
		// tags (replay-safe). First rule whose Condition matches wins: Skip aborts the application,
		// OverrideValue swaps the value source. If no rule matches, the modifier applies as-is.
		UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "GMCAbilitySystem|Conditions")
		TArray<FGMCModifierCondition> Conditions;

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