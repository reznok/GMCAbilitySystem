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

	if (PendingModifier.bValueIsAttribute)
	{
		if (!PendingModifier.SourceAbilitySystemComponent.IsValid())
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to add a modifier with bValueIsAttribute set to true, but no SourceAbilitySystemComponent was provided for Attribute: %s"), *Tag.ToString());
			return;
		}

		const FAttribute* Attribute = PendingModifier.SourceAbilitySystemComponent->GetAttributeByTag(PendingModifier.ValueAsAttribute);
		if (!Attribute)
		{
			UE_LOG(LogGMCAbilitySystem, Error, TEXT("Tried to add a modifier with bValueIsAttribute set to true, but no attribute was found for ValueAsAttribute: %s"), *PendingModifier.ValueAsAttribute.ToString());
			return;
		}

		PendingModifier.Value = Attribute->Value;
	}
		
		
	switch (PendingModifier.Op)
	{
		case EModifierType::Add:
			if (DeltaTime != -1.f) {
				PendingModifier.Value *= DeltaTime;
			}
			break;
		case EModifierType::Multiply:
			{
				if (DeltaTime != -1.f) {
					PendingModifier.Value = FMath::Pow(PendingModifier.Value, DeltaTime);
				}
			}
			break;
		default:
			break;
	}

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
	float CurModVal = 0.0f;
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
	auto Agglomerate = [&CurModVal, &CurOp, this](float ValueToApply) {

		float Result = 0.f;
		
		switch (CurOp) {
			case EModifierType::Add:
				Result = ValueToApply + CurModVal;
				break;
			case EModifierType::Multiply:
				Result = ValueToApply * CurModVal;
				break;
			case EModifierType::AddMultiplyBaseValue:
				Result = ValueToApply + (BaseValue * CurModVal);
				break;
			case EModifierType::SetToBaseValue:
				Result = BaseValue;
				break;
			case EModifierType::Set:
				Result = CurModVal;
				break;
			default: ;
		}
		
		return Clamp.ClampValue(Result);
	};

	bool bIsNewStep = true;
	
	for (int i = 0; i < PendingModifiers.Num(); i++) {
		const FGMCAttributeModifier& Modifier = PendingModifiers[i];

		CurOp = Modifier.Op;
		if (bIsNewStep)
		{
			// If this is a multipler, set current value to 1.f
			CurModVal = Modifier.Op == EModifierType::Multiply || Modifier.Op == EModifierType::AddMultiplyBaseValue ? 1.f : 0.f;
		}
		
		float PreApplicationVal = Value;
		if (Modifier.bRegisterInHistory && !bIsNewStep) {
			PreApplicationVal = Agglomerate(Value);
		}

		bIsNewStep = false;
		CurModVal += Modifier.Value; // Add to the stack of current step
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
		}
	}

	
	
	PendingModifiers.Empty();

	return OldValue != Value;
}
