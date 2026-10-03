#include "UGMAS_TestEventRecorder.h"
#include "Ability/GMCAbility.h"
#include "Effects/GMCAbilityEffect.h"

void UGMAS_TestEventRecorder::Bind(UGMC_AbilitySystemComponent* InComponent)
{
	Component = InComponent;
	InComponent->OnEffectApplied.AddDynamic(this, &UGMAS_TestEventRecorder::OnEffectApplied);
	InComponent->OnEffectRemoved.AddDynamic(this, &UGMAS_TestEventRecorder::OnEffectRemoved);
	InComponent->OnAbilityActivated.AddDynamic(this, &UGMAS_TestEventRecorder::OnAbilityActivated);
	InComponent->OnAbilityEnded.AddDynamic(this, &UGMAS_TestEventRecorder::OnAbilityEnded);
	InComponent->OnAbilityCancelled.AddDynamic(this, &UGMAS_TestEventRecorder::OnAbilityCancelled);
	InComponent->OnCustomEvent.AddDynamic(this, &UGMAS_TestEventRecorder::OnCustomEvent);
}

void UGMAS_TestEventRecorder::OnEffectApplied(UGMCAbilityEffect* AppliedEffect)
{
	const int Id = AppliedEffect ? AppliedEffect->EffectData.EffectID : -2;
	AppliedIDs.Add(Id);
	AppliedQueryable.Add(Component.IsValid() && AppliedEffect && Component->GetEffectById(Id) == AppliedEffect);
	if (bRemoveOnApplied && Component.IsValid() && AppliedEffect)
	{
		Component->RemoveActiveAbilityEffect(AppliedEffect);
	}
	if (CancelAbilityOnEffectAppliedTag.IsValid() && Component.IsValid())
	{
		Component->CancelAbilitiesByTag(CancelAbilityOnEffectAppliedTag);
	}
}

void UGMAS_TestEventRecorder::OnEffectRemoved(UGMCAbilityEffect* RemovedEffect)
{
	RemovedIDs.Add(RemovedEffect ? RemovedEffect->EffectData.EffectID : -2);
}

void UGMAS_TestEventRecorder::OnAbilityActivated(UGMCAbility* Ability, FGameplayTag AbilityTag)
{
	AbilityActivatedCount++;
	if (CancelAbilityOnActivatedTag.IsValid() && Ability && Ability->AbilityTag.MatchesTag(CancelAbilityOnActivatedTag))
	{
		Ability->CancelAbility();
	}
}

void UGMAS_TestEventRecorder::OnAbilityEnded(UGMCAbility* Ability) { AbilityEndedCount++; }

void UGMAS_TestEventRecorder::OnAbilityCancelled(UGMCAbility* Ability) { AbilityCancelledCount++; }

void UGMAS_TestEventRecorder::OnCustomEvent(FGameplayTag EventTag, FInstancedStruct Payload) { CustomEvents.Add(EventTag); }
