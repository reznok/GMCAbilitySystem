// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "CreateTagMap.h"
#include "Effects/GMCAbilityEffect.h"
#include "UObject/Object.h"
#include "UAbilityCostEffect.generated.h"

UCLASS()
class UAbilityCostEffect : public UGMCAbilityEffect
{
	GENERATED_BODY()
public:
	UAbilityCostEffect()
	{
		auto TagMap = CreateTagMap();
		EffectData.bIsInstant = true;
		EffectData.bNegateEffectAtEnd = false;
		EffectData.EffectTag = TagMap["Effect.AbilityCost"];
		EffectData.Modifiers.Add({
			TagMap["Attribute.Points"],
			-10.f,
			EModifierType::Add,
		});
	}
};
