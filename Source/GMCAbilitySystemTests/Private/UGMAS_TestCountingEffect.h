// Effect stub that records the order of its own events.
#pragma once

#include "CoreMinimal.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestCountingEffect.generated.h"

UCLASS(NotBlueprintable, NotBlueprintType)
class UGMAS_TestCountingEffect : public UGMCAbilityEffect
{
	GENERATED_BODY()

public:
	// "Start", "Tick", "Period", "End" in the order the events fired.
	UPROPERTY() TArray<FString> Events;

	virtual void StartEffectEvent_Implementation() override { Events.Add(TEXT("Start")); }
	virtual void TickEvent_Implementation(float DeltaTime) override { Events.Add(TEXT("Tick")); }
	virtual void PeriodTickEvent_Implementation() override { Events.Add(TEXT("Period")); }
	virtual void EndEffectEvent_Implementation() override { Events.Add(TEXT("End")); }

	int Count(const TCHAR* Name) const { int N = 0; for (const FString& E : Events) { if (E == Name) { N++; } } return N; }
};
