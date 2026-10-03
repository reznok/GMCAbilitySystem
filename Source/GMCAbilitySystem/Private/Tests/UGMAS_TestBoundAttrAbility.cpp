#include "Tests/UGMAS_TestBoundAttrAbility.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"

// The tag is defined at module scope in GMAS_TestTags.cpp.
// We use the raw string here rather than including GMAS_TestTags.h to keep
// the Plugin's Tests folder free of GMAS_2 module dependencies.
static const FName StaminaTagName(TEXT("GMAS.Test.Attribute.Stamina"));

void UGMAS_TestBoundAttrAbility::BeginAbility()
{
	Super::BeginAbility();

	// Read StaminaMod from the CDO so per-test overrides are always honoured,
	// even when TryActivateAbility instantiates a fresh object from the CDO.
	const float Mod = GetDefault<UGMAS_TestBoundAttrAbility>()->StaminaMod;

	// Build an instant modifier: +Mod to GMAS.Test.Attribute.Stamina.
	// EGMASEffectType::Instant with bNegateEffectAtEnd=false → writes directly
	// to RawValue, which is the GMC-bound field.  Both client and server execute
	// this inside GenPredictionTick → identical RawValue → no correction.
	FGMCAttributeModifier Mod_Stamina;
	Mod_Stamina.AttributeTag   = FGameplayTag::RequestGameplayTag(StaminaTagName, false);
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
