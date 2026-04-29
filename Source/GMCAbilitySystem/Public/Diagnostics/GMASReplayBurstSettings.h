// © Deep Worlds — Replay-burst diagnostics for GMAS.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Templates/SubclassOf.h"
#include "GMASReplayBurstSettings.generated.h"

class UUserWidget;

/**
 * Project-wide configuration for the GMAS replay-burst diagnostic.
 *
 * The diagnostic counts replay OCCURRENCES (one event per frame in which
 * `CL_IsReplaying()` was true at any point during PredictionTick), NOT the
 * number of moves replayed. When the running count over `WindowSeconds`
 * crosses `BurstThreshold`, the per-component delegate
 * `UGMC_AbilitySystemComponent::OnReplayBurstDetected` fires, a warning is
 * logged, and (optionally) `WarningWidgetClass` is added to the local
 * player's viewport for `WidgetDurationSeconds`.
 *
 * Edit at: Project Settings → GMC Ability System → Replay Burst Diagnostics.
 * Stored in: DefaultGame.ini, [/Script/GMCAbilitySystem.GMASReplayBurstSettings].
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Replay Burst Diagnostics"))
class GMCABILITYSYSTEM_API UGMASReplayBurstSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGMASReplayBurstSettings();

	// Returns the section under which this lives in Project Settings ("GMC Ability System").
	virtual FName GetCategoryName() const override { return TEXT("GMC Ability System"); }

	/** Master switch — turn the whole diagnostic off without touching call sites. */
	UPROPERTY(Config, EditAnywhere, Category="Detection")
	bool bEnableDetection = true;

	/**
	 * Number of replay occurrences within `WindowSeconds` that triggers an alert.
	 * "Occurrence" = one frame in which a replay happened, regardless of how many
	 * moves were replayed in that frame. Sustained chain-replays will trip the
	 * alert ~once per `WindowSeconds` (the buffer is reset on fire).
	 */
	UPROPERTY(Config, EditAnywhere, Category="Detection", meta=(ClampMin="2", UIMin="2"))
	int32 BurstThreshold = 5;

	/**
	 * Sliding window in seconds used to count occurrences.
	 * Tighter window → noisier (catches fast bursts but false-positives on lag spikes).
	 * Wider window → quieter (only flags sustained chains).
	 */
	UPROPERTY(Config, EditAnywhere, Category="Detection", meta=(ClampMin="0.1", UIMin="0.1"))
	float WindowSeconds = 2.0f;

	/**
	 * Optional UMG widget class shown to the local player when an alert fires.
	 * Leave None to skip the widget — the delegate and log warning still fire.
	 * Soft-class to avoid hard-loading UMG at module init; the class is loaded
	 * lazily on first alert.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Visualization")
	TSoftClassPtr<UUserWidget> WarningWidgetClass;

	/** Z-order passed to AddToPlayerScreen / AddToViewport. Higher = on top. */
	UPROPERTY(Config, EditAnywhere, Category="Visualization")
	int32 WidgetZOrder = 100;

	/** Auto-remove the widget after this many seconds. <=0 leaves it persistent. */
	UPROPERTY(Config, EditAnywhere, Category="Visualization", meta=(ClampMin="0.0", UIMin="0.0"))
	float WidgetDurationSeconds = 3.0f;
};
