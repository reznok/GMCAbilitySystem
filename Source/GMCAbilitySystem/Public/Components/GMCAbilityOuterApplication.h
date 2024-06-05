#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"
#include "GMCAbilityOuterApplication.generated.h"

USTRUCT()
struct FGMCAcknowledgeId {
	GENERATED_BODY()

	UPROPERTY()
	TArray<int> Id = {};
};


UENUM()
enum EGMCOuterApplicationType {
	EGMC_AddEffect,
	EGMC_RemoveEffect,
};

USTRUCT(BlueprintType)
struct FGMCOuterEffectAdd {
	GENERATED_BODY()

	UPROPERTY()
	TSubclassOf<UGMCAbilityEffect> EffectClass;

	UPROPERTY()
	FGMCAbilityEffectData InitializationData;
};

USTRUCT(BlueprintType)
struct FGMCOuterEffectRemove {
	GENERATED_BODY()

	UPROPERTY()
	TArray<int> Ids = {};
};

USTRUCT(BlueprintType)
struct FGMCOuterApplicationWrapper {
	GENERATED_BODY()

	UPROPERTY()
	TEnumAsByte<EGMCOuterApplicationType> Type = EGMC_AddEffect;

	UPROPERTY()
	FInstancedStruct OuterApplicationData;

	UPROPERTY()
	int LateApplicationID = 0;
	
	float ClientGraceTimeRemaining = 0.f;

	template<typename T, typename... Args>
	static FGMCOuterApplicationWrapper Make(Args... args)
	{
		FGMCOuterApplicationWrapper Wrapper;
		Wrapper.OuterApplicationData = FInstancedStruct::Make<T>(args...);
		return Wrapper;
	}


};

template<> inline FGMCOuterApplicationWrapper FGMCOuterApplicationWrapper::Make<FGMCOuterEffectAdd>(TSubclassOf<UGMCAbilityEffect> Effect, FGMCAbilityEffectData InitializationData)
{
	FGMCOuterApplicationWrapper Wrapper;
	Wrapper.Type = EGMC_AddEffect;
	Wrapper.OuterApplicationData = FInstancedStruct::Make<FGMCOuterEffectAdd>();
	FGMCOuterEffectAdd& Data = Wrapper.OuterApplicationData.GetMutable<FGMCOuterEffectAdd>();
	Data.EffectClass = Effect;
	Data.InitializationData = InitializationData;
	return Wrapper;
}

template<> inline FGMCOuterApplicationWrapper FGMCOuterApplicationWrapper::Make<FGMCOuterEffectRemove>(TArray<int> Ids)
{
	FGMCOuterApplicationWrapper Wrapper;
	Wrapper.Type = EGMC_RemoveEffect;
	Wrapper.OuterApplicationData = FInstancedStruct::Make<FGMCOuterEffectRemove>();
	FGMCOuterEffectRemove& Data = Wrapper.OuterApplicationData.GetMutable<FGMCOuterEffectRemove>();
	Data.Ids = Ids;
	return Wrapper;
}
