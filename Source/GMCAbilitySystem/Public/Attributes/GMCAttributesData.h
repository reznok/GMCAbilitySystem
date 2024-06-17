// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"
#include "Attributes/GMCAttributeClamp.h"
#include "GMCAttributesData.generated.h"

/** Used only in the AttributesData Data Asset to instantiate attributes. */
USTRUCT()
struct FAttributeData{
	GENERATED_BODY()
	
	/** i.e. Attribute.Health */
	UPROPERTY(EditDefaultsOnly, meta=(Categories="Attribute"), Category = "GMCAbilitySystem")
	FGameplayTag AttributeTag;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	float DefaultValue = 0.f;

	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	FAttributeClamp Clamp;

	/** Should the variable be bound to the GMC? If False, it will be replicated normally and CANNOT be used for
	 * prediction. */
	UPROPERTY(EditDefaultsOnly, Category = "GMCAbilitySystem")
	bool bGMCBound = true;
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAttributesData : public UPrimaryDataAsset{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="AttributeData")
	TArray<FAttributeData> AttributeData;
};
