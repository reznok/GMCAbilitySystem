// © Deep Worlds — Network-timing tunables for GMAS.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GMASNetworkTimingSettings.generated.h"

/**
 * Server-wise tunables for the network-timing safety windows used by GMAS effects.
 *
 * Two parameters live here:
 *  - `ClientEffectApplicationTimeout`: how long the client holds a Predicted effect
 *    waiting for server confirmation before rolling it back.
 *  - `DefaultClientGraceTime`: the bilateral defer window used at Remove time for
 *    Ticking/Periodic effects so client and server end on the same logical move tick.
 *    A per-effect `FGMCAbilityEffectData::ClientGraceTime > 0` overrides this default
 *    on a per-instance basis (designers can extend the window for slow drains).
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
	 */
	UPROPERTY(Config, EditAnywhere, Category="Timing", meta=(ClampMin="0.05", UIMin="0.05", ForceUnits="s"))
	float DefaultClientGraceTime = 0.5f;
};
