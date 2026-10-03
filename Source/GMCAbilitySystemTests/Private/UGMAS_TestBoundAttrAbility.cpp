#include "UGMAS_TestBoundAttrAbility.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"

// GMAS.Test.Attribute.Stamina is registered as a native tag by the spec that uses this ability
// (a shared test helper header takes that over in a later task).
static const FName StaminaTagName(TEXT("GMAS.Test.Attribute.Stamina"));

void UGMAS_TestBoundAttrAbility::BeginAbility()
{
	Super::BeginAbility();
	if (AbilityState == EAbilityState::Ended) return;   // cancelled inside Super: no effect from an ended ability

	// The instance's own StaminaMod: TryActivateAbility copies every CDO property, so a per-test
	// override on the CDO reaches it.
	const float Mod = StaminaMod;

	// Build an instant modifier: +Mod to GMAS.Test.Attribute.Stamina.
	// EGMASEffectType::Instant with bNegateEffectAtEnd=false → writes directly
	// to RawValue, which is the GMC-bound field.  Both client and server execute
	// this inside GenPredictionTick → identical RawValue → no correction.
	FGMCAttributeModifier Mod_Stamina;
	Mod_Stamina.AttributeTag   = FGameplayTag::RequestGameplayTag(StaminaTagName, /*ErrorIfNotFound=*/true);
	Mod_Stamina.Op             = EModifierType::Add;
	Mod_Stamina.ModifierValue  = Mod;

	FGMCAbilityEffectData Data;
	Data.EffectType = EGMASEffectType::Instant;
	Data.Modifiers.Add(Mod_Stamina);

	int Handle, Id;
	UGMCAbilityEffect* Effect;
	OwnerAbilityComponent->ApplyAbilityEffect(
		UGMCAbilityEffect::StaticClass(), Data,
		EGMCAbilityEffectQueueType::Predicted,
		Handle, Id, Effect);

	// One-shot ability — done after applying the effect.
	EndAbility();
}
