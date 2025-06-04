#include "Attributes/GMCAttributes.h"

#include "GMCAbilityComponent.h"
#include "ToolMenusEditor.h"


void FModifierHistory::AddMoveHistory(UGMCAbilityEffect* InstigatorEffect, float ActionTimer, float Value,
                                      bool bIsBound)
{

	if (!InstigatorEffect)
	{
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to add a modifier history entry with a null InstigatorEffect"));
		return;
	}
	
	if (ActionTimer <= 0.f) {
		UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to add a modifier history entry with a negative ActionTimer for InstigatorEffect: %s"), *InstigatorEffect->GetName());
		return;
	}

	// If we aren't bound, directly inject that in the concatenated history
	if (!bIsBound)
	{
		if (ConcatenatedHistory.Num() > 0 && ConcatenatedHistory.Last().InstigatorEffect == InstigatorEffect){
			ConcatenatedHistory.Last().Value += Value;
			return;
		}
		
		ConcatenatedHistory.Add(FModifierHistoryEntry{InstigatorEffect, 0.f, Value});
		return;
	}
	
	// Concatenate to same attribute if we share the same attribute, and Action timer is the same OR we are not bound. 
	if (History.Num() > 0 && History.Last().ActionTimer == ActionTimer && History.Last().InstigatorEffect == InstigatorEffect){
			History.Last().Value += Value;
			return;
	}
	
	// If we didn't find any entry, add a new one
	History.Add(FModifierHistoryEntry{InstigatorEffect, ActionTimer, Value});
}

void FModifierHistory::CleanMoveHistory(const float CurrentActionTimer) 

{
	// Remove all entries with a destroyed effect
	
	// Remove Value Superior to the current action timer (they are in the "future" as we are replaying, and we don't want to keep them
	// because they will be reprocessed by the replay

	// Ignore removing the history, we are not in the past
	if (History.IsEmpty() || CurrentActionTimer >= History.Last().ActionTimer)
	{
		return;
	}
	
	for (int i = History.Num() - 1; i >= 0; --i)
	{
		if (History[i].ActionTimer > CurrentActionTimer)
		{
			History.RemoveAt(i);
		}
		else
		{
			return; // We are in the past, we don't want to remove any more entries
		}
	}
}

float FModifierHistory::ExtractFromMoveHistory(UGMCAbilityEffect* InstigatorEffect, bool bPurge)
{
	float Result = 0.f;


	int Idx = ConcatenatedHistory.IndexOfByPredicate([&](const FModifierHistoryEntry& Entry) {
		return Entry.InstigatorEffect == InstigatorEffect;
	});

	if (Idx != INDEX_NONE)
	{
		Result += ConcatenatedHistory[Idx].Value;
		if (bPurge)
		{
			ConcatenatedHistory.RemoveAt(Idx);
		}
	}
	
	TArray<FModifierHistoryEntry> HistoryEntries = History.FilterByPredicate([&](const FModifierHistoryEntry& Entry) {
		return Entry.InstigatorEffect == InstigatorEffect;
	});
	for (int i = HistoryEntries.Num() - 1; i >= 0; --i)
	{
		Result += HistoryEntries[i].Value;
		if (bPurge)
		{
			History.RemoveAt(i);
		}
	}

	return Result;
}

void FAttribute::AddModifier(FGMCAttributeModifier PendingModifier, float DeltaTime) const
{

	if (DeltaTime == 0.f) {
		return;
	}
	
	PendingModifier.RegisterModifierValue(this, DeltaTime);
	PendingModifiers.Add(PendingModifier);
}

bool FAttribute::ProcessPendingModifiers(float ActionTimer) const
{
	
	if (PendingModifiers.IsEmpty()) {
		return false;
	}

	float OldValue = Value;
	
	// We iterate trough the whole array, sort value Phase > Priority > Operator > bRegisterInHistory
	PendingModifiers.Sort([](const FGMCAttributeModifier& A, const FGMCAttributeModifier& B) {
		if (A.Phase != B.Phase) { return A.Phase < B.Phase; }

		if (A.Priority != B.Priority) {
			return A.Priority < B.Priority;
		}

		if (A.Op != B.Op) {
			return static_cast<uint8>(A.Op) < static_cast<uint8>(B.Op);
		}

		return A.bRegisterInHistory < B.bRegisterInHistory;
	});

	
	// We want to agglomerate all Modifier of the same Priority, Operator, and Phase, then apply it to the base value

	// Init value
	float StackedModVal = 0.0f;
	int CurPrio = PendingModifiers[0].Priority;
	EModifierType CurOp = PendingModifiers[0].Op;
	EGMCModifierPhase CurPhase = PendingModifiers[0].Phase;
	bool bRegisterInHistory = PendingModifiers[0].bRegisterInHistory;

	// Just an useful lambda to check if the current step is the same as the previous one
	auto IsSameStep = [&CurPrio, &CurOp, &CurPhase, &bRegisterInHistory] (const FGMCAttributeModifier& Modifier) { 
		return Modifier.Priority == CurPrio && Modifier.Op == CurOp && Modifier.Phase == CurPhase 
			&& Modifier.bRegisterInHistory == bRegisterInHistory;
	};

	// Lambda to agglomerate value depending of the operator
	// return the value after agglomeration
	auto Agglomerate = [&StackedModVal, &CurOp, this](float AttributeValue) {

		double NewValue = 0.f;
		
		switch (CurOp) {
			case EModifierType::Add:
				NewValue = AttributeValue + StackedModVal; // NewValue = AttributeValue + ModifierValue
				break;
			case EModifierType::Percentage:
				NewValue = AttributeValue * StackedModVal; // NewValue = AttributeValue * ModifierValue
				break;
			case EModifierType::AddPercentageBaseValue:
				NewValue = AttributeValue + (BaseValue * StackedModVal); // NewValue = AttributeValue + (BaseValue * ModifierValue)
				break;
			case EModifierType::AddPercentage:
				NewValue = AttributeValue + (AttributeValue * StackedModVal); // NewValue = AttributeValue + (BaseValue * ModifierValue)
			break;
			case EModifierType::PercentageBaseValue:
				NewValue = BaseValue * StackedModVal; // NewValue = BaseValue * ModifierValue
				break;
			case EModifierType::SetToBaseValue:
				NewValue = BaseValue; // NewValue = BaseValue
				break;
			case EModifierType::Set:
				NewValue = StackedModVal; // NewValue = ModifierValue
				break;
			default: ;
		}
		
		return Clamp.ClampValue(NewValue);
	};

	bool bIsNewStep = true;
	
	for (int i = 0; i < PendingModifiers.Num(); i++) {
		const FGMCAttributeModifier& Modifier = PendingModifiers[i];

		if (!Modifier.SourceAbilityEffect.IsValid())
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to apply a modifier without a valid SourceAbilityEffect for Attribute: %s"), *Tag.ToString());
			continue;
		}

		CurOp = Modifier.Op;
		
		float PreApplicationVal = Value;
		if (Modifier.bRegisterInHistory && !bIsNewStep) {
			PreApplicationVal = Agglomerate(Value);
		}

		bIsNewStep = false;
		StackedModVal += Modifier.GetRegisteredValue();  // Add to the stack of current step
		CurPhase = Modifier.Phase;
		CurPrio = Modifier.Priority;
		
		// We must register the change value in the history
		if (Modifier.bRegisterInHistory) {
			float DeltaValue = Agglomerate(Value) - PreApplicationVal;
			
			if (DeltaValue != 0.f)
			{
				//UE_LOG(LogGMCAbilitySystem, Log, TEXT("Register Attribute Modification for %s %f on attribute %f"), *Modifier.AttributeTag.ToString(), DeltaValue, PreApplicationVal)
				ModifierHistory.AddMoveHistory(Modifier.SourceAbilityEffect.Get(), ActionTimer, DeltaValue, bIsGMCBound);
			}
		}

		// Agglomerate if next step is different or if we are at the end of the array
		if (i == PendingModifiers.Num() - 1 || !IsSameStep(PendingModifiers[i + 1])) {
			
			Value = Agglomerate(Value);
			bIsNewStep = true; // Next step will be a new step
			StackedModVal = 0.f;
		}
	}

	
	
	PendingModifiers.Empty();

	return OldValue != Value;
}
