// © Deep Worlds — Network-timing tunables for GMAS.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GMASNetworkTimingSettings.generated.h"

/**
 * Server-wise tunables for the network-timing safety windows used by GMAS effects.
 *
 * Three parameters live here:
 *  - `ClientEffectApplicationTimeout`: how long the client holds a Predicted effect
 *    waiting for server confirmation before rolling it back.
 *  - `DefaultClientGraceTime`: the bilateral defer window used at Remove time for
 *    Ticking/Periodic effects so client and server end on the same logical move tick
 *    (0 = off: a removed effect ends at once on each side). A per-effect
 *    `FGMCAbilityEffectData::ClientGraceTime > 0` overrides this default on a per-instance
 *    basis (designers can extend the window for slow drains, or defer one effect while the
 *    project default is 0).
 *  - `ServerOperationGraceSeconds`: how long the server waits for the owning client to
 *    acknowledge a server operation before applying it on the server anyway.
 *
 * Defaults are sized for typical RTT (30-150 ms) + jitter + one server tick at 30 Hz
 * (≈33 ms). Raise above 0.5 s if shipping to high-latency regions (sat / 200+ ms RTT)
 * where the timeout would false-positive; lower only if the server tick rate is high
 * and the player base has very low RTT.
 *
 * Edit at: Project Settings → GMC Ability System → Network Timing.
 * Stored in: DefaultGame.ini, [/Script/GMCAbilitySystem.GMASNetworkTimingSettings].
 */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Network Timing"))
class GMCABILITYSYSTEM_API UGMASNetworkTimingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UGMASNetworkTimingSettings();

	// Returns the section under which this lives in Project Settings ("GMC Ability System").
	virtual FName GetCategoryName() const override { return TEXT("GMC Ability System"); }

	/**
	 * Max time (seconds) the client holds a Predicted effect awaiting server confirmation
	 * before cancelling it locally (rollback). Sized to cover RTT + jitter + one server tick.
	 *
	 * Raise to ~0.8-1.0 s if the player base includes high-latency regions; lower to ~0.3 s
	 * only on a tight-latency LAN-style deployment with 60 Hz server tick.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.05", UIMin="0.05", ForceUnits="s"))
	float ClientEffectApplicationTimeout = 0.5f;

	/**
	 * Default bilateral defer window (seconds) for Ticking/Periodic effect end. Both client
	 * and server arm `EndAtActionTimer = ActionTimer + this` so each side fires the same
	 * number of Tick / period-boundary applications before the effect actually ends.
	 *
	 * Per-effect override: set `FGMCAbilityEffectData::ClientGraceTime > 0` on the effect
	 * itself to use a different window for that effect only (sentinel 0 = use this default).
	 *
	 * 0 turns the deferral off project-wide: a removed Ticking/Periodic effect ends at once on
	 * each side (instant visual removal, at the cost of up to ~RTT of client/server drift in the
	 * applied amount). A per-effect ClientGraceTime > 0 still defers that effect.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.0", UIMin="0.0", ForceUnits="s"))
	float DefaultClientGraceTime = 0.5f;

	/**
	 * Seconds the server waits for the owning client to acknowledge a server operation (effect
	 * apply/removal, impulse, custom event) through its move stream before applying it on the
	 * server anyway, outside any move. Pawns without an acknowledging client (AI, unpossessed)
	 * use 0 and apply on their next ancillary tick regardless of this value.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.0", UIMin="0.0", ForceUnits="s"))
	float ServerOperationGraceSeconds = 1.0f;

	/**
	 * Ability convergence: a covered ability instance (one created from an activation operation, with a
	 * twin on the owning client) at least this old (seconds, local confirm clock) makes the server send
	 * periodic digests of its covered instances, and lets the client request one when none arrives.
	 * Younger instances converge through answers and mirrored ends alone.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.1", UIMin="0.1", ForceUnits="s"))
	float AbilityReconcileMinAge = 1.0f;

	/**
	 * Ability convergence: seconds between two server digests while a covered instance is at least
	 * AbilityReconcileMinAge old. The client ends a local covered instance missing from a digest only once
	 * it is AbilityReconcileMinAge + this old (margin for a digest already in flight).
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.1", UIMin="0.1", ForceUnits="s"))
	float AbilityDigestInterval = 1.0f;

	/**
	 * Ability convergence: seconds (local confirm clock) an answer or end is held when it arrives before
	 * the instance it names exists on the receiving side; it is applied when the instance is created and
	 * dropped after this time.
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.1", UIMin="0.1", ForceUnits="s"))
	float AbilityAnswerHoldTime = 5.0f;
};
