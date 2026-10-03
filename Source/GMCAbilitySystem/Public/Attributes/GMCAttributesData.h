// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Attributes/GMCAttributeClamp.h"
#include "Replication/SyncSettings.h" // EGMC_CombineMode
#include "GMCAttributesData.generated.h"

/** Used only in the AttributesData Data Asset to instantiate attributes. */
USTRUCT()
struct FAttributeData{
	GENERATED_BODY()
	
	/** i.e. Attribute.Health */
	UPROPERTY(EditDefaultsOnly, meta=(Categories="Attribute"), Category = "GMCAbilitySystem")
	FGameplayTag AttributeTag;

	// When true, the attribute starts at its resolved upper clamp instead of DefaultValue.
	// Requires Clamp.Max (literal) or Clamp.MaxAttributeTag to be set. When set, DefaultValue
	// is ignored. Re-evaluated on each Init() — works with MaxAttributeTag-based clamps via
	// the two-pass init in UGMC_AbilitySystemComponent.
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bStartFull = false;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem", meta = (EditCondition = "!bStartFull", EditConditionHides))
	float DefaultValue = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	FAttributeClamp Clamp;

	/** Should the variable be bound to the GMC? If False, it will be replicated normally and CANNOT be used for
	 * prediction. */
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bGMCBound = true;

	/** Combine mode for this attribute's GMC bind (applied to BOTH Value and RawValue).
	 *
	 * Default CombineIfUnchanged = legacy behaviour: any per-tick change to the attribute forces its own
	 * (uncombined) client move, so a discrete change lands on its own move boundary for replay.
	 *
	 * Set to AlwaysCombineOverwrite ONLY for an attribute that changes continuously every prediction tick
	 * (e.g. a per-frame Stamina drain/regen) AND does NOT feed the movement integration. Such an attribute
	 * otherwise defeats GMC move-combining every frame (extra client->server moves = dedicated-server cost);
	 * AlwaysCombineOverwrite keeps the freshest value and lets the move combine.
	 *
	 * UNSAFE for any attribute that gates movement (Speed / acceleration / friction): a combined move re-runs
	 * one ExecuteMove over the summed dt using the END value for the whole window, which would diverge from the
	 * server's re-derivation and trigger a ClientValidated correction. Validate any change by client correction
	 * count (LogGMCReplication "was not valid"), never by dedicated-server FPS. */
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	EGMC_CombineMode ValueCombineMode = EGMC_CombineMode::CombineIfUnchanged;
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAttributesData : public UPrimaryDataAsset{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="AttributeData", meta=(TitleProperty="{AttributeTag} ({DefaultValue})"))
	TArray<FAttributeData> AttributeData;
};
