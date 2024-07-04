#pragma once

#include "CoreMinimal.h"
#include "KismetCastingUtils.h"
#include "Effects/GMCAbilityEffect.h"
#include "UObject/Object.h"

inline TMap<FName, FGameplayTag> CreateTagMap()
{
	TMap<FName, FGameplayTag> TagMap;
	
	FGameplayTagContainer Container;
	UGameplayTagsManager::Get().RequestAllGameplayTags(Container, true);
	TArray<FGameplayTag> TagArray = Container.GetGameplayTagArray();

	TagArray.Sort();

	TagMap.Add(FName("Attribute.Points"), TagArray[0]);
	TagMap.Add(FName("Effect.AbilityCost"), TagArray[1]);

	return TagMap;
}
