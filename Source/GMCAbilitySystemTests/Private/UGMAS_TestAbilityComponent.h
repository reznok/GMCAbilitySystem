// Test seed hook for the attribute-initialization specs. SetAttributeInitialValue answers from a
// map instead of the Blueprint event, so a case can drive the hook without a Blueprint; a real
// override would fall through to the Blueprint event (Super) for the tags it does not seed.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestAbilityComponent.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestAbilityComponent : public UGMC_AbilitySystemComponent
{
	GENERATED_BODY()

public:
	// Tag -> initial value the hook returns for that attribute.
	UPROPERTY()
	TMap<FGameplayTag, float> InitialValueOverrides;

	virtual void SetAttributeInitialValue(const FGameplayTag& AttributeTag, float& BaseValue) override
	{
		if (const float* V = InitialValueOverrides.Find(AttributeTag)) { BaseValue = *V; }
	}
};
