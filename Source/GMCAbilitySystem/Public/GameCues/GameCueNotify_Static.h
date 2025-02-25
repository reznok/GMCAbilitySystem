// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Effects/GMCAbilityEffectTypes.h"
#include "GameCueNotify_Static.generated.h"

/**
 *	A non instantiated UObject that acts as a handler for a GameCue. These are useful for one-off "burst" effects.
 */

UCLASS(Blueprintable, meta = (ShowWorldContextPin), hidecategories = (Replication))
class GMCABILITYSYSTEM_API UGameCueNotify_Static : public UObject
{
	GENERATED_UCLASS_BODY()

	/** Does this GameCueNotify handle this type of GameCueEvent? */
	virtual bool HandlesEvent(EGameCueEvent::Type EventType) const;

	virtual void OnOwnerDestroyed();

	virtual void PostInitProperties() override;

	virtual void Serialize(FArchive& Ar) override;

	virtual void HandleGameCue(AActor* MyTarget, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters);

	UWorld* GetWorld() const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif // WITH_EDITOR

	/** Generic Event Graph event that will get called for every event type */
	UFUNCTION(BlueprintImplementableEvent, Category = "GameCueNotify", DisplayName = "HandleGameCue", meta=(ScriptName = "HandleGameCue"))
	void K2_HandleGameCue(AActor* MyTarget, EGameCueEvent::Type EventType, const FGameCueParameters& Parameters) const;

	/** Called when a GameCue is executed, this is used for instant effects or periodic ticks */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "GameCueNotify")
	bool OnExecute(AActor* MyTarget, const FGameCueParameters& Parameters) const;

	/** Called when a GameCue with duration is first activated, this will only be called if the client witnessed the activation */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "GameCueNotify")
	bool OnActive(AActor* MyTarget, const FGameCueParameters& Parameters) const;

	/** Called when a GameCue with duration is first seen as active, even if it wasn't actually just applied (Join in progress, etc) */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "GameCueNotify")
	bool WhileActive(AActor* MyTarget, const FGameCueParameters& Parameters) const;

	/** Called when a GameCue with duration is removed */
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category = "GameCueNotify")
	bool OnRemove(AActor* MyTarget, const FGameCueParameters& Parameters) const;

	/** Tag this notify is activated by */
	UPROPERTY(EditDefaultsOnly, Category = GameCue, meta=(Categories="GameCue"))
	FGameplayTag	GameCueTag;

	/** Mirrors GameCueTag in order to be asset registry searchable */
	UPROPERTY(AssetRegistrySearchable)
	FName GameCueName;

	/** Does this Cue override other cues, or is it called in addition to them? E.g., If this is Damage.Physical.Slash, we wont call Damage.Physical afer we run this cue. */
	UPROPERTY(EditDefaultsOnly, Category = GameCue)
	bool IsOverride;

private:
	virtual void DeriveGameCueTagFromAssetName();
};
