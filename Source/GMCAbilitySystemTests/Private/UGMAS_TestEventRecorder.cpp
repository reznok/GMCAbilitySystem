#include "UGMAS_TestEventRecorder.h"
#include "Effects/GMCAbilityEffect.h"

void UGMAS_TestEventRecorder::Bind(UGMC_AbilitySystemComponent* InComponent)
{
	Component = InComponent;
	InComponent->OnEffectApplied.AddDynamic(this, &UGMAS_TestEventRecorder::OnEffectApplied);
	InComponent->OnEffectRemoved.AddDynamic(this, &UGMAS_TestEventRecorder::OnEffectRemoved);
	InComponent->OnAbilityEnded.AddDynamic(this, &UGMAS_TestEventRecorder::OnAbilityEnded);
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
}

void UGMAS_TestEventRecorder::OnEffectRemoved(UGMCAbilityEffect* RemovedEffect)
{
	RemovedIDs.Add(RemovedEffect ? RemovedEffect->EffectData.EffectID : -2);
}

void UGMAS_TestEventRecorder::OnAbilityEnded(UGMCAbility* Ability) { AbilityEndedCount++; }

void UGMAS_TestEventRecorder::OnCustomEvent(FGameplayTag EventTag, FInstancedStruct Payload) { CustomEvents.Add(EventTag); }
