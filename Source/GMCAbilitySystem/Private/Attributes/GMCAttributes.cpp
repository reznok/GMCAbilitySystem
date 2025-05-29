#include "Attributes/GMCAttributes.h"

#include "GMCAbilityComponent.h"


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

void FAttribute::ProcessPendingModifiers(TArray<FModifierApplicationEntry>& ModifierHistory) const
{
	if (PendingModifiers.IsEmpty()) {
		return;
	}

	
	// We iterate trough the whole array, sort value Phase > Operator > Priority
	PendingModifiers.Sort([](const FGMCAttributeModifier& A, const FGMCAttributeModifier& B) {
		if (A.Phase != B.Phase) { return A.Phase < B.Phase; }

		if (A.Priority != B.Priority) {
			return A.Priority < B.Priority;
		}
		
		return static_cast<uint8>(A.Op) < static_cast<uint8>(B.Op);
	});

	
	// We want to agglomerate all Modifier of the same Priority, Operator, and Phase, then apply it to the base value

	// Init value
	float CurModVal = 0.0f;
	int CurPrio = PendingModifiers[0].Priority;
	EModifierType CurOp = PendingModifiers[0].Op;
	EGMCModifierPhase CurPhase = PendingModifiers[0].Phase;

	// Just an useful lambda to check if the current step is the same as the previous one
	auto IsSameStep = [&CurPrio, &CurOp, &CurPhase] (const FGMCAttributeModifier& Modifier) { 
		return Modifier.Priority == CurPrio && Modifier.Op == CurOp && Modifier.Phase == CurPhase;
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

		//UE_LOG(LogGMCAbilitySystem, Log, TEXT("Adding Modifier %f to %f"), CurModVal, Modifier.Value);
		CurModVal += Modifier.Value; // Add to the stack of current step
		
		CurPhase = Modifier.Phase;
		
		CurPrio = Modifier.Priority;

		/*UE_LOG(LogGMCAbilitySystem, Log, TEXT("Process %s on Attribute %s (Value: %s (%s)%s-> %s) | Phase: %s(%d))"),
			*GetNameSafe(Modifier.SourceAbilityEffect.Get()),
			*Tag.ToString(),
			*FString::SanitizeFloat(PreApplicationVal),
			Modifier.Op == EModifierType::Set ? TEXT("=") : Modifier.Op == EModifierType::Add ? TEXT("+") : TEXT("*"),
			*FString::SanitizeFloat(CurModVal),
			*FString::SanitizeFloat(Agglomerate(Value)),
			*UEnum::GetValueAsString(Modifier.Phase),
			Modifier.Priority);*/

		// We must register the change value in the history
		if (Modifier.bRegisterInHistory) {
			float DeltaValue = Agglomerate(Value) - PreApplicationVal;
			
			if (DeltaValue != 0.f)
			{
				//UE_LOG(LogGMCAbilitySystem, Log, TEXT("Register Attribute Modification for %s %f on attribute %f"), *Modifier.AttributeTag.ToString(), DeltaValue, PreApplicationVal)
				ModifierHistory.Add(FModifierApplicationEntry(Modifier.SourceAbilityEffect,
					Modifier.AttributeTag,
					Modifier.SourceAbilitySystemComponent.IsValid() ? Modifier.SourceAbilitySystemComponent->ActionTimer : 0.f,
					DeltaValue,
					bIsGMCBound));
			}
		}

		// Agglomerate if next step is different or if we are at the end of the array
		if (i == PendingModifiers.Num() - 1 || !IsSameStep(PendingModifiers[i + 1])) {
			
			Value = Agglomerate(Value);
			bIsNewStep = true; // Next step will be a new step
		}
	}
	
	PendingModifiers.Empty();
}
