#pragma once
#include "NativeGameplayTags.h"

#include "GMASAIEvents.generated.h"

class UGMCAbility;
// On Attribute Changed
UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GMAS_AI_Event_AttributeChanged)
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEMAI_API FGMAS_AIEventAttributeChanged
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTag AttributeTag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float OldValue;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float NewValue;
};

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GMAS_AI_Event_ActiveTagsChanged)
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEMAI_API FGMAS_AIEventActiveTagsChanged
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer AddedTags;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FGameplayTagContainer RemovedTags;
};

UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_GMAS_AI_Event_AbilityEnded)
USTRUCT(BlueprintType)
struct GMCABILITYSYSTEMAI_API FGMAS_AIEventAbilityEnded
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	UGMCAbility* Ability;
};