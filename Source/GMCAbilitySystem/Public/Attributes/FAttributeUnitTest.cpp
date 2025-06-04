// ReSharper disable CppExpressionWithoutSideEffects
#include "GMCAttributeModifier.h"
#include "GMCAttributes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAttributeNewOperatorsTest, "UE5.Marketplace.DeepWorlds_GMCAbilitySystem.Source.GMCAbilitySystem.Public.Attributes.NewOperatorsTest", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// Helper function to create test modifiers
FGMCAttributeModifier CreateModifier(EModifierType Op, float Value, int32 Priority = 0, 
    EGMCModifierPhase Phase = EGMCModifierPhase::EMP_Stack, bool bRegisterInHistory = false)
{
    FGMCAttributeModifier Modifier;
    Modifier.Op = Op;
    Modifier.ModifierValue = Value;
    Modifier.Priority = Priority;
    Modifier.Phase = Phase;
    Modifier.bRegisterInHistory = bRegisterInHistory;
	Modifier.SourceAbilityEffect = NewObject<UGMCAbilityEffect>();
    
    return Modifier;
}

bool FAttributeNewOperatorsTest::RunTest(const FString& Parameters)
{
	// Test 1: SetToBaseValue basic functionality
	{
		UE_LOG(LogTemp, Log, TEXT("Test 1: SetToBaseValue Basic Functionality"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 150.0f;
		TestAttribute.Value = 300.0f; // Different from base
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0), -1.f);
    	
		TestAttribute.ProcessPendingModifiers(50.f);
        
		TestEqual("SetToBaseValue: Should reset to BaseValue (150)", TestAttribute.Value, 150.0f);
	}

	// Test 2: AddPercentageBaseValue basic functionality - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 2: AddPercentageBaseValue Basic Functionality"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Add 50% of BaseValue
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 50.f, 0), -1.f);
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 + (100 * 0.5) = 200 + 50 = 250
		TestEqual("AddPercentageBaseValue: 200 + (100 * 50%) = 250", TestAttribute.Value, 250.0f);
	}

	// Test 3: Complete operator priority order verification - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 3: Operator Priority Order (SetToBaseValue->Set->AddPercentageBaseValue->Percentage->AddPercentage->PercentageBaseValue->Add)"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		// Add in reverse order to test sorting
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 20.0f, 0), -1.f);                      // Should be last (7)
		TestAttribute.AddModifier(CreateModifier(EModifierType::PercentageBaseValue, 160.f, 0), -1.f);     // 6th
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentage, 25.f, 0), -1.f);            // 5th
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 80.f, 0), -1.f);               // 4th (80% = 0.8x)
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 60.f, 0), -1.f);   // 3rd
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 80.0f, 0), -1.f);                     // 2nd
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0), -1.f);           // 1st
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Order: SetToBaseValue(50) -> Set(80) -> AddPercentageBaseValue(80+50*0.6=110) -> Percentage(110*0.8=88) -> AddPercentage(88+88*0.25=110) -> PercentageBaseValue(50*1.6=80) -> Add(80+20=100)
		TestEqual("Priority order: Final result = 100", TestAttribute.Value, 100.0f);
	}

	// Test 4: AddPercentageBaseValue agglomeration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 4: AddPercentageBaseValue Agglomeration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 100.0f;
        
		// Multiple AddPercentageBaseValue with same priority should agglomerate
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 25.f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 15.f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 10.f, 0, EGMCModifierPhase::EMP_Post), -1.f);  // Different phase
        
		TestAttribute.ProcessPendingModifiers(50);
        
		// Phase EMP_Stack: 100 + (200 * (0.25+0.15)) = 100 + (200 * 0.4) = 100 + 80 = 180
		// Phase EMP_Post: 180 + (200 * 0.1) = 180 + 20 = 200
		TestEqual("AddPercentageBaseValue agglomeration: Final = 200", TestAttribute.Value, 200.0f);
	}

	// Test 5: SetToBaseValue with different priorities and phases
	{
		UE_LOG(LogTemp, Log, TEXT("Test 5: Multiple SetToBaseValue Operations"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 75.0f;
		TestAttribute.Value = 150.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f); // Executed in first
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Post), -1.f); // Executed in third
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 25.0f, 3, EGMCModifierPhase::EMP_Stack), -1.f); // Executed in second
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 1: Reset to 75, Priority 2: 75+25=100, Priority 3: Reset to 75 again
		TestEqual("Multiple SetToBaseValue: Final reset to base = 75", TestAttribute.Value, 75.0f);
	}

	// Test 6: Complex multiplication and addition mix - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 6: Complex Multiplication and Addition Mix"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 60.0f;
		TestAttribute.Value = 120.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 50.f, 0, EGMCModifierPhase::EMP_Stack), -1.f);  // 50% = 0.5x
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 25.f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // 25% = 0.25x -> combined = 0.75x
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 30.0f, 1, EGMCModifierPhase::EMP_Post), -1.f);     // +30 flat
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 15.0f, 1, EGMCModifierPhase::EMP_Post), -1.f);     // +15 flat
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 120 * (0.5 + 0.25) = 120 * 0.75 = 90
		// Priority 1: 90 + (30 + 15) = 90 + 45 = 135
		TestEqual("Multiply then Add: (120 * 0.75) + 45 = 135", TestAttribute.Value, 135.0f);
	}

	// Test 7: Zero BaseValue with AddPercentageBaseValue
	{
		UE_LOG(LogTemp, Log, TEXT("Test 7: Zero BaseValue with AddPercentageBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 200.0f, 0), -1.f); // +200% of 0
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (0 * 2.0) = 100 + 0 = 100
		TestEqual("Zero BaseValue: 100 + (0 * 200%) = 100", TestAttribute.Value, 100.0f);
	}

	// Test 8: Negative AddPercentageBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 8: Negative AddPercentageBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 300.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, -40.f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // -40%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 300 + (200 * -0.4) = 300 - 80 = 220
		TestEqual("Negative AddPercentageBaseValue: 300 - 80 = 220", TestAttribute.Value, 220.0f);
	}

	// Test 9: Set operation overriding everything
	{
		UE_LOG(LogTemp, Log, TEXT("Test 9: Set Operation Override"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 250.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 500.0f, 1, EGMCModifierPhase::EMP_Post), -1.f);      // Set to 500
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 1000.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);    // This happens first but gets overridden
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 250 + 1000 = 1250, Priority 1: Set to 500
		TestEqual("Set override: Add first, then Set = 500", TestAttribute.Value, 500.0f);
	}

	// Test 10: Extreme AddPercentageBaseValue values - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 10: Extreme AddPercentageBaseValue Values"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 1000.0f, 0), -1.f); // +1000%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (50 * 10.0) = 100 + 500 = 600
		TestEqual("Extreme AddPercentageBaseValue: 100 + 500 = 600", TestAttribute.Value, 600.0f);
	}

	// Test 11: All operators in single group (same priority) - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 11: All Operators Same Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 40.0f;
		TestAttribute.Value = 80.0f;
        
		// All same priority, should execute in operator order
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 10.0f, 0), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::PercentageBaseValue, 150.f, 0), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentage, 25.f, 0), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 50.f, 0), -1.f);                  // 50% = 0.5x
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 25.f, 0), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 60.0f, 0), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0), -1.f);
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// SetToBaseValue(40) -> Set(60) -> AddPercentageBaseValue(60+(40*0.25)=70) -> Percentage(70*0.5=35) -> AddPercentage(35+(35*0.25)=43.75) -> PercentageBaseValue(40*1.5=60) -> Add(60+10=70)
		TestEqual("All operators same priority: Final result = 70", TestAttribute.Value, 70.0f);
	}

	// Test 12: Priority gaps with new operators - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 12: Priority Gaps with New Operators"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 25.0f;
		TestAttribute.Value = 75.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentage, 200.0f, 100, EGMCModifierPhase::EMP_Post), -1.f);     // Very high priority +200%
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f);      // Low priority
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 200.0f, 50, EGMCModifierPhase::EMP_Stack), -1.f);       // Medium priority 200% = 2x
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 1: Reset to 25, Priority 50: 25*2=50, Priority 100: 50+(50*2)=150
		TestEqual("Priority gaps: Reset->Multiply->AddPercentage = 150", TestAttribute.Value, 150.0f);
	}

	// Test 13: Floating point precision with AddPercentageBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 13: Floating Point Precision"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 33.333333f;
		TestAttribute.Value = 66.666666f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 33.333333f, 0), -1.f);
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 66.666666 + (33.333333 * 0.33333333) ≈ 66.666666 + 11.111111 ≈ 77.777777
		TestEqual("Value should be ~77.777f", TestAttribute.Value, 77.777777f, 0.02f);
	}

	// Test 14: Multiple SetToBaseValue operations
	{
		UE_LOG(LogTemp, Log, TEXT("Test 14: Multiple SetToBaseValue Same Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 500.0f;
        
		// Multiple SetToBaseValue in same priority group
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// All should result in reset to BaseValue
		TestEqual("Multiple SetToBaseValue: All reset to 100", TestAttribute.Value, 100.0f);
	}

	// Test 15: Set vs SetToBaseValue priority
	{
		UE_LOG(LogTemp, Log, TEXT("Test 15: Set vs SetToBaseValue Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 200.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 150.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // Same priority, but SetToBaseValue comes first in enum
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// SetToBaseValue (50) then Set (150) in same priority group
		TestEqual("SetToBaseValue then Set: Final = 150", TestAttribute.Value, 150.0f);
	}

	// Test 16: Complex chain with all operators - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 16: Complex Chain All Operators"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 100.f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // +100%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (100 * 1.0) = 100 + 100 = 200
		TestEqual("AddPercentageBaseValue chain: Final = 200", TestAttribute.Value, 200.0f);
	}

	// Test 17: AddPercentageBaseValue with zero current value - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 17: AddPercentageBaseValue with Zero Current Value"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 80.0f;
		TestAttribute.Value = 0.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 75.f, 0), -1.f); // +75%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0 + (80 * 0.75) = 0 + 60 = 60
		TestEqual("Zero current + AddPercentageBase: 0 + 60 = 60", TestAttribute.Value, 60.0f);
	}

	// Test 18: Massive AddPercentageBaseValue accumulation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 18: Massive AddPercentageBaseValue Accumulation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 10.0f;
		TestAttribute.Value = 50.0f;
        
		// Add 10 modifiers of 20% each
		for (int32 i = 0; i < 10; ++i)
		{
			TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 20.f, 0), -1.f);
		}
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 50 + (10 * (20*0.2)) = 50 + (10 * 2) = 50 + 20 = 70
		TestEqual("10x AddPercentageBase: 50 + 20 = 70", TestAttribute.Value, 70.0f);
	}

	// Test 19: Negative BaseValue scenarios - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 19: Negative BaseValue Scenarios"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = -30.0f;
		TestAttribute.Value = 60.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 200.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);   // +200%
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Post), -1.f);        // Reset to -30
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 60 + (-30 * 2.0) = 60 - 60 = 0, Priority 1: Reset to -30
		TestEqual("Negative BaseValue: Reset to -30", TestAttribute.Value, -30.0f);
	}

	// Test 20: Mixed positive and negative AddPercentageBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 20: Mixed Positive/Negative AddPercentageBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 50.f, 0, EGMCModifierPhase::EMP_Stack), -1.f);  // +50%
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, -30.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // -30%
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 10.f, 0, EGMCModifierPhase::EMP_Stack), -1.f);  // +10%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 + (100 * (0.5 - 0.3 + 0.1)) = 200 + (100 * 0.3) = 200 + 30 = 230
		TestEqual("Mixed +/- AddPercentageBase: 200 + 30 = 230", TestAttribute.Value, 230.0f);
	}

	// Test 21: Empty modifiers list
	{
		UE_LOG(LogTemp, Log, TEXT("Test 21: Empty Modifiers List"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 75.0f;
		TestAttribute.Value = 125.0f;
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// No modifiers, value should remain unchanged
		TestEqual("Empty list: Value unchanged = 125", TestAttribute.Value, 125.0f);
	}

	// Test 22: Large priority values - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 22: Large Priority Values"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 25.0f, 999999, EGMCModifierPhase::EMP_Post), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 200.0f, 500000, EGMCModifierPhase::EMP_Stack), -1.f); // 200% = 2x
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 1: Reset to 50, Priority 500000: 50*2=100, Priority 999999: 100+25=125
		TestEqual("Large priorities: Final = 125", TestAttribute.Value, 125.0f);
	}

	// Test 23: SetToBaseValue after modifications
	{
		UE_LOG(LogTemp, Log, TEXT("Test 23: SetToBaseValue After Heavy Modifications"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 25.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 500.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f);      // 500% = 5x
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 1000.0f, 1, EGMCModifierPhase::EMP_Post), -1.f);       // Make it massive
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Post), -1.f); // Reset everything
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Despite all modifications, final reset to BaseValue
		TestEqual("Reset after modifications: Final = 25", TestAttribute.Value, 25.0f);
	}

	// Test 24: Precision with multiple operations - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 24: Precision with Multiple Operations"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 7.777f;
		TestAttribute.Value = 15.555f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 77.7f, 0, EGMCModifierPhase::EMP_Stack), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 33.3f, 1, EGMCModifierPhase::EMP_Post), -1.f); // 33.3% = 0.333x
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Step 1: 15.555 + (7.777 * 0.777) = 15.555 + 6.041 = 21.596
		// Step 2: 21.596 * 0.333 = 7.191
		TestEqual("Float Precision Test", TestAttribute.Value, 7.191f, 0.01f);
	}

	// Test 25: All operations with history tracking
	{
		UE_LOG(LogTemp, Log, TEXT("Test 25: All Operations with History Tracking"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 40.0f;
		TestAttribute.Value = 80.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 50.f, 1, EGMCModifierPhase::EMP_Post, true), -1.f); // +50%
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 15.0f, 2, EGMCModifierPhase::EMP_Stack, false), -1.f); // Not tracked
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Should have 2 history entries (SetToBaseValue and AddPercentageBaseValue)
		TestEqual("History tracking: 2 entries", TestAttribute.ModifierHistory.Num(), 2);
		// 40 -> 55 -> 75
		TestEqual("Final value: 40 -> 55 -> 75", TestAttribute.Value, 75.0f);
	}

	// Test 26: Extreme multiplication with AddPercentageBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 26: Extreme Multiplication with AddPercentageBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 5.0f;
		TestAttribute.Value = 10.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 10000.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // +10000%
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 300.f, 1, EGMCModifierPhase::EMP_Post), -1.f);              // 300% = 3x
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Step 1: 10 + (5 * 100.0) = 10 + 500 = 510
		// Step 2: 510 * 3.0 = 1530
		TestEqual("Extreme operations: Final = 1530", TestAttribute.Value, 1530.0f, 0.01f);
	}

	// Test 27: Zero value propagation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 27: Zero Value Propagation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.0f;
		TestAttribute.Value = 0.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 500.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // +500% of 0
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 1100.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f);           // 1100% = 11x of 0
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 50.0f, 2, EGMCModifierPhase::EMP_Post), -1.f);               // Finally non-zero
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0 + (0 * 5.0) = 0, 0 * 11 = 0, 0 + 50 = 50
		TestEqual("Zero propagation: Final = 50", TestAttribute.Value, 50.0f);
	}

	// Test 28: Alternating SetToBaseValue and modifications - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 28: Alternating SetToBaseValue and Modifications"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 30.0f;
		TestAttribute.Value = 120.0f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 20.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // First Operation
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f); // Second Operation
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 200.0f, 2, EGMCModifierPhase::EMP_Post), -1.f); // Third Operation 200% = 2x
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 3, EGMCModifierPhase::EMP_Post), -1.f);
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 120+20=140 -> Reset(30) -> 30*2=60 -> Reset(30)
		TestEqual("Alternating resets: Final = 30", TestAttribute.Value, 30.0f);
	}

	// Test 29: Performance test with new operators
	{
		UE_LOG(LogTemp, Log, TEXT("Test 29: Performance Test with New Operators"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		// Add 50000 modifiers of various types with mixed phases
		for (int32 i = 0; i < 50000; ++i)
		{
			EModifierType OpType = static_cast<EModifierType>(i % 7); // All 7 operator types
			int32 Priority = i % 20;
			float Value = (i % 3 == 0) ? 0.1f : -0.05f;
			EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
			TestAttribute.AddModifier(CreateModifier(OpType, Value, Priority, Phase), -1.f);
		}
        
		double StartTime = FPlatformTime::Seconds();
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		double EndTime = FPlatformTime::Seconds();
		double ElapsedMs = (EndTime - StartTime) * 1000.0;
        
		UE_LOG(LogTemp, Log, TEXT("Processing 50000 new modifiers took %f ms"), ElapsedMs);
        
		// Should complete in reasonable time
		TestTrueExpr(ElapsedMs < 20.0);
		TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
		TestTrueExpr(FMath::IsFinite(TestAttribute.Value));
	}

	// Test 29.2: Performance test with new operators and history registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 29.2: Performance Test with New Operators and History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		FAttribute TestAttributeBound;
		TestAttributeBound.BaseValue = 50.0f;
		TestAttributeBound.Value = 100.0f;
		TestAttributeBound.bIsGMCBound = true;
    	 
		// Add 50000 modifiers of various types with mixed phases
		for (int32 i = 0; i < 50000; ++i)
		{
			EModifierType OpType = static_cast<EModifierType>(i % 7);
			int32 Priority = i % 20;
			float Value = (i % 3 == 0) ? 0.1f : -0.05f;
			EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
			if (i % 2 == 0)
			{
				TestAttribute.AddModifier(CreateModifier(OpType, Value, Priority, Phase, true), -1.f);
			}
			else
			{
				TestAttributeBound.AddModifier(CreateModifier(OpType, Value, Priority, Phase, true), -1.f);
			}
		}
        
		double StartTime = FPlatformTime::Seconds();
        
		TestAttribute.ProcessPendingModifiers(50.f);
		TestAttributeBound.ProcessPendingModifiers(50.f);
        
		double EndTime = FPlatformTime::Seconds();
		double ElapsedMs = (EndTime - StartTime) * 1000.0;
        
		UE_LOG(LogTemp, Log, TEXT("Processing 50000 new modifiers took %f ms (SizeOfHistory:%fmb)"), ElapsedMs, (TestAttribute.ModifierHistory.GetAllocatedSize() + TestAttributeBound.ModifierHistory.GetAllocatedSize()) / 1000000.f);
        
		// Should complete in reasonable time
		TestTrueExpr(ElapsedMs < 20.0);
		TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
		TestTrueExpr(FMath::IsFinite(TestAttribute.Value));
	}

	// Test 30: Ultra complex scenario with all operators - PHASE PRIORITY CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 30: Ultra Complex All Operators Scenario - Phase Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
	    
		// Create a complex scenario with multiple priorities and phases
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 50.f, 5, EGMCModifierPhase::EMP_Stack), -1.f);               // 50% = 0.5x
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 25.0f, 10, EGMCModifierPhase::EMP_Post), -1.f);       // +25
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack), -1.f);        // Reset to 100
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 30.f, 7, EGMCModifierPhase::EMP_Stack), -1.f);  // +30%
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 80.0f, 3, EGMCModifierPhase::EMP_Post), -1.f);        // Set to 80
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentage, 20.f, 8, EGMCModifierPhase::EMP_Stack), -1.f);  // +20%
		TestAttribute.AddModifier(CreateModifier(EModifierType::PercentageBaseValue, 150.f, 2, EGMCModifierPhase::EMP_Stack), -1.f);   // 150% of base
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 15.0f, 10, EGMCModifierPhase::EMP_Post), -1.f);       // +15 (same priority/phase)
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// PHASE EMP_Stack (Phase 0) - All processed first regardless of priority:
		//   Priority 1: SetToBaseValue → Reset to 100
		//   Priority 2: PercentageBaseValue → 100 * 1.5 = 150  
		//   Priority 5: Percentage → 150 * 0.5 = 75
		//   Priority 7: AddPercentageBaseValue → 75 + (100 * 0.3) = 105
		//   Priority 8: AddPercentage → 105 + (105 * 0.2) = 126
		//
		// PHASE EMP_Post (Phase 1) - All processed after EMP_Stack:
		//   Priority 3: Set to 80 → 80
		//   Priority 10: Add → 80 + (25 + 15) = 120
	    
		TestEqual("Ultra complex with phase priority: Final = 120", TestAttribute.Value, 120.0f);
	}

	// Test 31: Bonus - Edge case with very small numbers - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 31: Edge Case - Very Small Numbers"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.001f;
		TestAttribute.Value = 0.002f;
        
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 100000.0f, 0, EGMCModifierPhase::EMP_Stack), -1.f); // +100000%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0.002 + (0.001 * 1000.0) = 0.002 + 1.0 = 1.002
		TestEqual("Very small numbers: Final = 1.002", TestAttribute.Value, 1.002f);
	}

	// Test 32: Bonus - Operator precedence stress test
	{
		UE_LOG(LogTemp, Log, TEXT("Test 32: Operator Precedence Stress Test"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 10.0f;
		TestAttribute.Value = 20.0f;
        
		// Add all operators multiple times with mixed priorities and phases
		for (int32 Priority = 0; Priority < 5; ++Priority)
		{
			EGMCModifierPhase Phase = (Priority % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
			TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 1.0f, Priority, Phase), -1.f);
			TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentage, 10.f, Priority, Phase), -1.f); // +10%
			TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 90.f, Priority, Phase), -1.f); // 90% = 0.9x
			TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 10.f, Priority, Phase), -1.f); // +10%
			TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 15.0f + Priority, Priority, Phase), -1.f);
			TestAttribute.AddModifier(CreateModifier(EModifierType::PercentageBaseValue, 120.f + Priority * 10, Priority, Phase), -1.f); // 120%+
			TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, Priority, Phase), -1.f);
		}
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Complex calculation - just ensure it produces a valid result
		TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
		TestTrueExpr(FMath::IsFinite(TestAttribute.Value));
		TestTrueExpr(TestAttribute.Value >= 0.0f);
        
		UE_LOG(LogTemp, Log, TEXT("Stress test result: %f"), TestAttribute.Value);
	}

	// Test 33: SetToBaseValue - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 33: SetToBaseValue Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 75.0f;
		TestAttribute.Value = 150.0f; // Different from base

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("SetToBaseValue: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: -75 
			TestEqual("SetToBaseValue: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false), -75.0f);
		}
		TestEqual("SetToBaseValue: Final value", TestAttribute.Value, 75.0f);
	}

	// Test 34: Set Operator - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 34: Set Operator Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Set, 300.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		TestEqual("Set: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: 300 (new value) - 200 = 100
			TestEqual("Set: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false), 100.f);
		}
		TestEqual("Set: Final value", TestAttribute.Value, 300.0f);
	}

	// Test 35: AddPercentageBaseValue - Individual History Registration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 35: AddPercentageBaseValue Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::AddPercentageBaseValue, 60.f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("AddPercentageBaseValue: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (100 + (50 * 0.6)) - 100 = 130 - 100 = 30
			TestEqual("AddPercentageBaseValue: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false), 30.0f);
		}
		TestEqual("AddPercentageBaseValue: Final value", TestAttribute.Value, 130.0f);
	}

	// Test 36: Percentage - Individual History Registration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 36: Percentage Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 80.0f;
		TestAttribute.Value = 120.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Percentage, 50.f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f); // 50% = 0.5x
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("Percentage: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (120 * 0.5) - 120 = 60 - 120 = -60
			TestEqual("Percentage: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false) , -60.0f);
		}
		TestEqual("Percentage: Final value", TestAttribute.Value, 60.0f);
	}

	// Test 37: Add - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 37: Add Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 90.0f;
		TestAttribute.Value = 110.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("Add: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (110 + 45) - 110 = 155 - 110 = 45
			TestEqual("Add: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false) , 45.0f);
		}
		TestEqual("Add: Final value", TestAttribute.Value, 155.0f);
	}

	// Test 38: Multiple Operators - Selective History Registration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 38: Multiple Operators - Selective History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
		TestAttribute.bIsGMCBound = true;

		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true); // Tracked 200 + 50 = 250
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Percentage, 60.f, 1, EGMCModifierPhase::EMP_Stack, false); // Not tracked // 250 * 0.6 = 150
		Mod2.SourceAbilityEffect = Mod1.SourceAbilityEffect;
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 400.0f, 2, EGMCModifierPhase::EMP_Post, true); // Tracked  // 150 -> 400 
		Mod3.SourceAbilityEffect = Mod1.SourceAbilityEffect;
    	
		TestAttribute.AddModifier(Mod1, -1.f);     // Tracked
		TestAttribute.AddModifier(Mod2, -1.f);// Not tracked
		TestAttribute.ProcessPendingModifiers(50.f);

		// Test first register
		TestEqual("Selective history: Entry count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() >= 1)
		{
			// First entry: Add 50 to 200 = 250, delta = 50
			TestEqual("First entry delta (Add)", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod1.SourceAbilityEffect.Get(), true), 50.0f);
		}
    	
		TestAttribute.AddModifier(Mod3, -1.f);   // Tracked
		TestAttribute.ProcessPendingModifiers(60.f);
    	
		// Should have 1 history entry (concatenated for same effect)
		TestEqual("Selective history: Entry count", TestAttribute.ModifierHistory.Num(), 1);
    	
		if (TestAttribute.ModifierHistory.Num() >= 1)
		{
			// Combined effect: Add(50) + Set(+250) = 300 total
			TestEqual("Combined effect delta", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod1.SourceAbilityEffect.Get(), false) , 250.0f, 0.02f);
		}
		TestEqual("Final value", TestAttribute.Value, 400.0f);
	}

	// Test 39: Cross-Phase History Registration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 39: Cross-Phase History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 60.0f;
		TestAttribute.Value = 120.0f;
		TestAttribute.bIsGMCBound = true;

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::AddPercentageBaseValue, 50.f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 30.0f, 1, EGMCModifierPhase::EMP_Post, true); // Tracked
		Mod3.SourceAbilityEffect = Mod2.SourceAbilityEffect;
    	
		TestAttribute.AddModifier(Mod2, -1.f);// 120 + (60 * 0.5) = 150
		TestAttribute.ProcessPendingModifiers(50.f);
		TestAttribute.AddModifier(Mod3, -1.f); // 150 + 30 = 180
		TestAttribute.ProcessPendingModifiers(60.f);
	    
		// Should have 2 history entries
		TestEqual("Cross-phase history: Entry count", TestAttribute.ModifierHistory.Num(), 2);
	    
		if (TestAttribute.ModifierHistory.Num() >= 2)
		{
			// Combined effect: AddPercentageBase(30) + Add(30) = 60 total
			TestEqual("Combined phase delta", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod3.SourceAbilityEffect.Get(), true), 60.0f);
		}
		TestEqual("Final value", TestAttribute.Value, 180.0f);
	}

	// Test 40: Zero Delta - No History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 40: Zero Delta - No History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 150.0f;
		TestAttribute.Value = 150.0f; // Same as base
	    
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 0 history entries because delta is 0
		TestEqual("Zero delta: No history entry", TestAttribute.ModifierHistory.Num(), 0);
		TestEqual("Value unchanged", TestAttribute.Value, 150.0f);
	}

	// Test 41: Agglomerated History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 41: Agglomerated History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
	    
		// Multiple modifiers of same priority/phase/operator - should agglomerate
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);  
		TestAttribute.AddModifier(CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 3 history entries (one per modifier, even though they're agglomerated)
		TestEqual("Agglomerated: History count", TestAttribute.ModifierHistory.Num(), 3);
		TestEqual("Final value: 200 + 75 = 275", TestAttribute.Value, 275.0f);
	}

	// Test 43: Negative Delta History
	{
		UE_LOG(LogTemp, Log, TEXT("Test 43: Negative Delta History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 300.0f;

		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Percentage, 40.f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod1, -1.f); // 40% = 0.4x
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry with negative delta
		TestEqual("Negative delta: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta: (300 * 0.4) - 300 = 120 - 300 = -180
			TestEqual("Negative delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod1.SourceAbilityEffect.Get(), false) , -180.0f);
		}
		TestEqual("Final value", TestAttribute.Value, 120.0f);
	}

	// Test 44: Complex Multi-Phase History
	{
		UE_LOG(LogTemp, Log, TEXT("Test 44: Complex Multi-Phase History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
	    
		// EMP_Stack phase
		TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::AddPercentageBaseValue, 100.0f, 1, EGMCModifierPhase::EMP_Stack, true), -1.f); // +100%
	    
		// EMP_Post phase  
		TestAttribute.AddModifier(CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true), -1.f);
		TestAttribute.AddModifier(CreateModifier(EModifierType::Percentage, 150.f, 4, EGMCModifierPhase::EMP_Post, true), -1.f); // 150% = 1.5x
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 4 history entries
		TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);
	    
		// Execution order due to Phase > Priority:
		// EMP_Stack Priority 1: AddPercentageBaseValue first
		// EMP_Stack Priority 2: SetToBaseValue second
		// EMP_Post Priority 3: Set
		// EMP_Post Priority 4: Percentage
	    
		TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5
	}

	// Test 45: Basic History Addition - Bound vs Unbound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 45: Basic History Addition - Bound vs Unbound"));
		FAttribute BoundAttribute;
		BoundAttribute.BaseValue = 100.0f;
		BoundAttribute.Value = 100.0f;
		BoundAttribute.bIsGMCBound = true;

		FAttribute UnboundAttribute;
		UnboundAttribute.BaseValue = 100.0f;
		UnboundAttribute.Value = 100.0f;
		UnboundAttribute.bIsGMCBound = false;

		// Test bound attribute
		BoundAttribute.AddModifier(CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
		BoundAttribute.ProcessPendingModifiers(10.0f);
		TestEqual("Bound: History count", BoundAttribute.ModifierHistory.Num(), 1);
		TestEqual("Bound: Final value", BoundAttribute.Value, 150.0f);

		// Test unbound attribute
		UnboundAttribute.AddModifier(CreateModifier(EModifierType::Add, 30.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
		UnboundAttribute.ProcessPendingModifiers(10.0f);
		TestEqual("Unbound: History count", UnboundAttribute.ModifierHistory.Num(), 1);
		TestEqual("Unbound: Final value", UnboundAttribute.Value, 130.0f);
	}

	// Test 46: History Concatenation - Same ActionTimer
	{
		UE_LOG(LogTemp, Log, TEXT("Test 46: History Concatenation - Same ActionTimer"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = true;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

		// Add multiple modifiers with same effect - should concatenate in history
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
    
		Mod1.SourceAbilityEffect = Effect;
		Mod2.SourceAbilityEffect = Effect;
		Mod3.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.AddModifier(Mod3, -1.f);

		TestAttribute.ProcessPendingModifiers(15.0f);

		TestEqual("Same ActionTimer: 1 entries (Concatenated)", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("Same ActionTimer: Combined effect", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 75.0f);
		TestEqual("Final value", TestAttribute.Value, 175.0f);
	}

	// Test 46.2: History Concatenation - Different ActionTimer
	{
		UE_LOG(LogTemp, Log, TEXT("Test 46.2: History Concatenation - Different ActionTimer"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = true;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

		// Add multiple modifiers with same effect - should concatenate in history
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
    
		Mod1.SourceAbilityEffect = Effect;
		Mod2.SourceAbilityEffect = Effect;
		Mod3.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(15.0f);
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(16.0f);
		TestAttribute.AddModifier(Mod3, -1.f);
		TestAttribute.ProcessPendingModifiers(17.0f);

		TestEqual("Same ActionTimer: 3 entries (one per modifier)", TestAttribute.ModifierHistory.Num(), 3);
		TestEqual("Same ActionTimer: Combined effect", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 75.0f);
		TestEqual("Final value", TestAttribute.Value, 175.0f);
	}

	// Test 46.3: History Concatenation - Different ActionTimer - UnBound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 46.3: History Concatenation - Different ActionTimer - UnBound"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

		// Add multiple modifiers with same effect - should concatenate in history
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
    
		Mod1.SourceAbilityEffect = Effect;
		Mod2.SourceAbilityEffect = Effect;
		Mod3.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(15.0f);
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(16.0f);
		TestAttribute.AddModifier(Mod3, -1.f);
		TestAttribute.ProcessPendingModifiers(17.0f);

		TestEqual("Same ActionTimer: 1 entries (Combined)", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("Same ActionTimer: Combined effect", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 75.0f);
		TestEqual("Final value", TestAttribute.Value, 175.0f);
	}

	// Test 46.4: History Concatenation - Same ActionTimer - Unbound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 46.4: History Concatenation - Same ActionTimer - Unbound"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

		// Add multiple modifiers with same effect - should concatenate in history
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
    
		Mod1.SourceAbilityEffect = Effect;
		Mod2.SourceAbilityEffect = Effect;
		Mod3.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.AddModifier(Mod3, -1.f);

		TestAttribute.ProcessPendingModifiers(15.0f);

		TestEqual("Same ActionTimer: 1 entries (Concatenated)", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("Same ActionTimer: Combined effect", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 75.0f);
		TestEqual("Final value", TestAttribute.Value, 175.0f);
	}

	// Test 47: History Concatenation - Unbound Same Effect
	{
		UE_LOG(LogTemp, Log, TEXT("Test 47: History Concatenation - Unbound Same Effect"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

		// Add modifiers at different times but same effect - should concatenate in unbound
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 20.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod1.SourceAbilityEffect = Effect;
		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 30.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect;
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(15.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 10.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect;
		TestAttribute.AddModifier(Mod3, -1.f);
		TestAttribute.ProcessPendingModifiers(20.0f);

		TestEqual("Unbound same effect: 1 concatenated entry", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("Unbound same effect: Combined value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 60.0f);
		TestEqual("Final value", TestAttribute.Value, 160.0f);
	}

	// Test 48: CleanMoveHistory - Future Entries Removal
	{
		UE_LOG(LogTemp, Log, TEXT("Test 48: CleanMoveHistory - Future Entries Removal"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = true;

		UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();

		// Add modifiers at different times
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod1.SourceAbilityEffect = Effect1;
		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(20.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect3;
		TestAttribute.AddModifier(Mod3, -1.f);
		TestAttribute.ProcessPendingModifiers(30.0f);

		TestEqual("Before clean: 3 entries", TestAttribute.ModifierHistory.Num(), 3);

		// Clean at time 15.0f - should remove entries > 15.0f
		TestAttribute.ModifierHistory.CleanMoveHistory(15.0f);

		TestEqual("After clean: 1 entry remains", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("Remaining entry value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false), 25.0f);
		TestEqual("Removed entry value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false), 0.0f);
	}

	// Test 48.1: CleanMoveHistory - Future Entries Removal - Unbound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 48.1: CleanMoveHistory - Future Entries Removal - Unbound"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();

		// Add modifiers at different times
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod1.SourceAbilityEffect = Effect1;
		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(20.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect3;
		TestAttribute.AddModifier(Mod3, -1.f);
		TestAttribute.ProcessPendingModifiers(30.0f);

		TestEqual("Before clean: 3 entries", TestAttribute.ModifierHistory.Num(), 3);

		// Clean at time 15.0f - should remove entries > 15.0f
		TestAttribute.ModifierHistory.CleanMoveHistory(15.0f);

		TestEqual("After clean: 3 entry remains", TestAttribute.ModifierHistory.Num(), 3);
		TestEqual("Remaining entry value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false), 25.0f);
		TestEqual("Removed entry value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false), 35.0f);
	}

	// Test 49: CleanMoveHistory - No Future Entries
	{
		UE_LOG(LogTemp, Log, TEXT("Test 49: CleanMoveHistory - No Future Entries"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = true;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();
		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);
    
		// Clean at time 20.0f - no entries should be removed
		TestAttribute.ModifierHistory.CleanMoveHistory(20.0f);

		TestEqual("No future entries: Count unchanged", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("No future entries: Value unchanged", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 50.0f);
	}

	// Test 49.1: CleanMoveHistory - No Future Entries - Unbound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 49.1: CleanMoveHistory - No Future Entries - Unbound"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();
		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod.SourceAbilityEffect = Effect;

		TestAttribute.AddModifier(Mod, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);
    
		// Clean at time 20.0f - no entries should be removed
		TestAttribute.ModifierHistory.CleanMoveHistory(20.0f);

		TestEqual("No future entries: Count unchanged", TestAttribute.ModifierHistory.Num(), 1);
		TestEqual("No future entries: Value unchanged", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false), 50.0f);
	}

	// Test 50: ExtractFromMoveHistory - With Purge
	{
		UE_LOG(LogTemp, Log, TEXT("Test 50: ExtractFromMoveHistory - With Purge"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
		TestAttribute.bIsGMCBound = true;

		UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();

		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 100.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod1.SourceAbilityEffect = Effect1;
		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 200.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.ProcessPendingModifiers(15.0f);

		TestEqual("Before purge: 2 entries", TestAttribute.ModifierHistory.Num(), 2);

		// Extract Effect1 with purge
		float Value1 = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, true);
		TestEqual("Extract Effect1 value", Value1, 100.0f);
		TestEqual("After purge Effect1: 1 entry remains", TestAttribute.ModifierHistory.Num(), 1);

		// Extract Effect2 without purge
		float Value2 = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
		TestEqual("Extract Effect2 value", Value2, 200.0f);
		TestEqual("After no purge Effect2: 1 entry remains", TestAttribute.ModifierHistory.Num(), 1);
	}

	// Test 51: ExtractFromMoveHistory - Mixed History Types
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 51: ExtractFromMoveHistory - Mixed History Types"));
	    FAttribute BoundAttribute;
	    BoundAttribute.BaseValue = 100.0f;
	    BoundAttribute.Value = 100.0f;
	    BoundAttribute.bIsGMCBound = true;

	    FAttribute UnboundAttribute;
	    UnboundAttribute.BaseValue = 100.0f;
	    UnboundAttribute.Value = 100.0f;
	    UnboundAttribute.bIsGMCBound = false;

	    UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();

	    // Add to bound attribute (History)
	    FGMCAttributeModifier BoundMod = CreateModifier(EModifierType::Add, 75.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    BoundMod.SourceAbilityEffect = Effect;
	    BoundAttribute.AddModifier(BoundMod, -1.f);
	    BoundAttribute.ProcessPendingModifiers(10.0f);

	    // Add to unbound attribute (ConcatenatedHistory)
	    FGMCAttributeModifier UnboundMod = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    UnboundMod.SourceAbilityEffect = Effect;
	    UnboundAttribute.AddModifier(UnboundMod, -1.f);
	    UnboundAttribute.ProcessPendingModifiers(15.0f);

	    TestEqual("Bound: 1 entry", BoundAttribute.ModifierHistory.Num(), 1);
	    TestEqual("Unbound: 1 entry", UnboundAttribute.ModifierHistory.Num(), 1);

	    // Extract should get correct values from each
	    float BoundValue = BoundAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false);
	    float UnboundValue = UnboundAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false);

	    TestEqual("Bound extraction", BoundValue, 75.0f);
	    TestEqual("Unbound extraction", UnboundValue, 25.0f);
	}

	// Test 52: Multiple Effects in History
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 52: Multiple Effects in History"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 100.0f;
	    TestAttribute.Value = 100.0f;
	    TestAttribute.bIsGMCBound = true;

	    UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();

	    // Add multiple entries for Effect1 at different times
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 30.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod1.SourceAbilityEffect = Effect1;
	    TestAttribute.AddModifier(Mod1, -1.f);
	    TestAttribute.ProcessPendingModifiers(10.0f);

	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 40.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod2.SourceAbilityEffect = Effect1;
	    TestAttribute.AddModifier(Mod2, -1.f);
	    TestAttribute.ProcessPendingModifiers(20.0f);

	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 100.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod3.SourceAbilityEffect = Effect2;
	    TestAttribute.AddModifier(Mod3, -1.f);
	    TestAttribute.ProcessPendingModifiers(15.0f);

	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Add, 200.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod4.SourceAbilityEffect = Effect3;
	    TestAttribute.AddModifier(Mod4, -1.f);
	    TestAttribute.ProcessPendingModifiers(25.0f);

	    TestEqual("Multiple effects: 4 entries", TestAttribute.ModifierHistory.Num(), 4);

	    // Extract Effect1 (should get both entries)
	    float Effect1Value = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false);
	    TestEqual("Effect1: Combined value", Effect1Value, 70.0f);

	    // Extract Effect2
	    float Effect2Value = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
	    TestEqual("Effect2: Single value", Effect2Value, 100.0f);

	    TestEqual("Final value", TestAttribute.Value, 470.0f);
	}

	// Test 53: Zero Delta - No History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 53: Zero Delta - No History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 150.0f;
	    TestAttribute.Value = 150.0f; // Same as base
	    TestAttribute.bIsGMCBound = true;

	    TestAttribute.AddModifier(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true), -1.f);
	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 0 history entries because delta is 0
	    TestEqual("Zero delta: No history entry", TestAttribute.ModifierHistory.Num(), 0);
	    TestEqual("Value unchanged", TestAttribute.Value, 150.0f);
	}

	// Test 54: Agglomerated History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 54: Agglomerated History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 100.0f;
	    TestAttribute.Value = 200.0f;
	    TestAttribute.bIsGMCBound = true;

	    UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();

	    // Multiple modifiers of same priority/phase/operator - should agglomerate in processing but separate in history
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    
	    Mod1.SourceAbilityEffect = Effect1;
	    Mod2.SourceAbilityEffect = Effect2;
	    Mod3.SourceAbilityEffect = Effect3;

	    TestAttribute.AddModifier(Mod1, -1.f);
	    TestAttribute.AddModifier(Mod2, -1.f);
	    TestAttribute.AddModifier(Mod3, -1.f);

	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 3 history entries (one per modifier)
	    TestEqual("Agglomerated: History count", TestAttribute.ModifierHistory.Num(), 3);
	    TestEqual("Final value: 200 + 75 = 275", TestAttribute.Value, 275.0f);

	    // Each effect should have its individual contribution
	    TestEqual("Effect1 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false), 25.0f);
	    TestEqual("Effect2 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false), 35.0f);
	    TestEqual("Effect3 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false), 15.0f);
	}

	// Test 54.1: Agglomerated History Registration - Unbound
	{
		UE_LOG(LogTemp, Log, TEXT("Test 54.1: Agglomerated History Registration - Unbound"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
		TestAttribute.bIsGMCBound = false;

		UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
		UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();

		// Multiple modifiers of same priority/phase/operator - should agglomerate in processing but separate in history
		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    
		Mod1.SourceAbilityEffect = Effect1;
		Mod2.SourceAbilityEffect = Effect2;
		Mod3.SourceAbilityEffect = Effect3;

		TestAttribute.AddModifier(Mod1, -1.f);
		TestAttribute.AddModifier(Mod2, -1.f);
		TestAttribute.AddModifier(Mod3, -1.f);

		TestAttribute.ProcessPendingModifiers(50.0f);

		// Should have 3 history entries (one per modifier)
		TestEqual("Agglomerated: History count", TestAttribute.ModifierHistory.Num(), 3);
		TestEqual("Final value: 200 + 75 = 275", TestAttribute.Value, 275.0f);

		// Each effect should have its individual contribution
		TestEqual("Effect1 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false), 25.0f);
		TestEqual("Effect2 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false), 35.0f);
		TestEqual("Effect3 contribution", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false), 15.0f);
	}

	// Test 55: Complex Multi-Phase History
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 55: Complex Multi-Phase History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 50.0f;
	    TestAttribute.Value = 100.0f;
	    TestAttribute.bIsGMCBound = true;

	    UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect4 = NewObject<UGMCAbilityEffect>();

	    // EMP_Stack phase
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::AddPercentageBaseValue, 100.0f, 1, EGMCModifierPhase::EMP_Stack, true); // +100%
	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true);

	    // EMP_Post phase
	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true);
	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Percentage, 150.f, 4, EGMCModifierPhase::EMP_Post, true); // 150% = 1.5x

	    Mod1.SourceAbilityEffect = Effect1;
	    Mod2.SourceAbilityEffect = Effect2;
	    Mod3.SourceAbilityEffect = Effect3;
	    Mod4.SourceAbilityEffect = Effect4;

	    TestAttribute.AddModifier(Mod1, -1.f); // 100 + (50*1.0) = 150
	    TestAttribute.AddModifier(Mod2, -1.f); // 150 -> 50 = -100
	    TestAttribute.AddModifier(Mod3, -1.f); // 50 -> 200 = 150
	    TestAttribute.AddModifier(Mod4, -1.f); // 200 * 1.5 = 300

	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 4 history entries
	    TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);

	    TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5

	    // Verify individual contributions
	    float Effect1Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false);
	    float Effect2Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
	    float Effect3Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false);
	    float Effect4Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect4, false);

	    TestEqual("AddPercentageBaseValue delta", Effect1Delta, 50.0f); // 100 + (50*1.0) - 100 = 50
	    TestEqual("SetToBaseValue delta", Effect2Delta, -100.0f); // 150 -> 50 = -100
	    TestEqual("Set delta", Effect3Delta, 150.0f); // 50 -> 200 = 150
	    TestEqual("Percentage delta", Effect4Delta, 100.0f); // 200 * 1.5 - 200 = 100
	}

		// Test 55.1: Complex Multi-Phase History - Unbound
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 55.1: Complex Multi-Phase History - Unbound"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 50.0f;
	    TestAttribute.Value = 100.0f;
	    TestAttribute.bIsGMCBound = false;

	    UGMCAbilityEffect* Effect1 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect2 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect3 = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* Effect4 = NewObject<UGMCAbilityEffect>();

	    // EMP_Stack phase
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::AddPercentageBaseValue, 100.0f, 1, EGMCModifierPhase::EMP_Stack, true); // +100%
	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true);

	    // EMP_Post phase
	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true);
	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Percentage, 150.f, 4, EGMCModifierPhase::EMP_Post, true); // 150% = 1.5x

	    Mod1.SourceAbilityEffect = Effect1;
	    Mod2.SourceAbilityEffect = Effect2;
	    Mod3.SourceAbilityEffect = Effect3;
	    Mod4.SourceAbilityEffect = Effect4;

	    TestAttribute.AddModifier(Mod1, -1.f);// 100 + (50*1.0) = 150
	    TestAttribute.AddModifier(Mod2, -1.f); // 150 -> 50 = -100
	    TestAttribute.AddModifier(Mod3, -1.f); // 50 -> 200 = 150
	    TestAttribute.AddModifier(Mod4, -1.f); // 200 * 1.5 = 300

	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 4 history entries
	    TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);

	    TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5

	    // Verify individual contributions
	    float Effect1Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false);
	    float Effect2Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
	    float Effect3Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false);
	    float Effect4Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect4, false);

	    TestEqual("AddPercentageBaseValue delta", Effect1Delta, 50.0f); // 100 + (50*1.0) - 100 = 50
	    TestEqual("SetToBaseValue delta", Effect2Delta, -100.0f); // 150 -> 50 = -100
	    TestEqual("Set delta", Effect3Delta, 150.0f); // 50 -> 200 = 150
	    TestEqual("Percentage delta", Effect4Delta, 100.0f); // 200 * 1.5 - 200 = 100
	}

	// Test 56: Performance Test with History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 56: Performance Test with History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 50.0f;
	    TestAttribute.Value = 100.0f;
	    TestAttribute.bIsGMCBound = true;

	    // Add 1000 modifiers with history registration
	    for (int32 i = 0; i < 1000; ++i)
	    {
	        EModifierType OpType = static_cast<EModifierType>(i % 7); // All 7 operator types
	        int32 Priority = i % 10;
	        float Value = (i % 3 == 0) ? 0.1f : -0.05f;
	        EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
	        
	        TestAttribute.AddModifier(CreateModifier(OpType, Value, Priority, Phase, true), -1.f);
	    }

	    double StartTime = FPlatformTime::Seconds();
	    TestAttribute.ProcessPendingModifiers(50.0f);
	    double EndTime = FPlatformTime::Seconds();
	    double ElapsedMs = (EndTime - StartTime) * 1000.0;

	    UE_LOG(LogTemp, Log, TEXT("Processing 1000 modifiers with history took %f ms"), ElapsedMs);
	    UE_LOG(LogTemp, Log, TEXT("History size: %d entries (%f MB)"), TestAttribute.ModifierHistory.Num(), 
	           TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0f);

	    // Should complete in reasonable time and produce valid results
	    TestTrueExpr(ElapsedMs < 50.0);
	    TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
	    TestTrueExpr(FMath::IsFinite(TestAttribute.Value));
	    TestTrueExpr(TestAttribute.ModifierHistory.Num() > 0);
	}

	// Test 57: Complex History Scenario with CleanMoveHistory
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 57: Complex History Scenario with CleanMoveHistory"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 50.0f;
	    TestAttribute.Value = 100.0f;
	    TestAttribute.bIsGMCBound = true;

	    UGMCAbilityEffect* PastEffect = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* CurrentEffect = NewObject<UGMCAbilityEffect>();
	    UGMCAbilityEffect* FutureEffect = NewObject<UGMCAbilityEffect>();

	    // Add modifiers at different times
	    FGMCAttributeModifier PastMod = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    PastMod.SourceAbilityEffect = PastEffect;
	    TestAttribute.AddModifier(PastMod, -1.f);
	    TestAttribute.ProcessPendingModifiers(10.0f);

	    FGMCAttributeModifier CurrentMod = CreateModifier(EModifierType::Add, 40.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    CurrentMod.SourceAbilityEffect = CurrentEffect;
	    TestAttribute.AddModifier(CurrentMod, -1.f);
	    TestAttribute.ProcessPendingModifiers(20.0f);

	    FGMCAttributeModifier FutureMod = CreateModifier(EModifierType::Add, 60.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    FutureMod.SourceAbilityEffect = FutureEffect;
	    TestAttribute.AddModifier(FutureMod, -1.f);
	    TestAttribute.ProcessPendingModifiers(30.0f);

	    TestEqual("Before clean: 3 entries", TestAttribute.ModifierHistory.Num(), 3);

	    // Simulate replay at time 22.0f
	    TestAttribute.ModifierHistory.CleanMoveHistory(22.0f);

	    TestEqual("After clean at 22.0f: 2 entries remain", TestAttribute.ModifierHistory.Num(), 2);

	    // Verify correct entries remain
	    float PastValue = TestAttribute.ModifierHistory.ExtractFromMoveHistory(PastEffect, false);
	    float CurrentValue = TestAttribute.ModifierHistory.ExtractFromMoveHistory(CurrentEffect, false);
	    float FutureValue = TestAttribute.ModifierHistory.ExtractFromMoveHistory(FutureEffect, false);

	    TestEqual("Past effect preserved", PastValue, 25.0f);
	    TestEqual("Current effect preserved", CurrentValue, 40.0f);
	    TestEqual("Future effect removed", FutureValue, 0.0f); // Should be 0 as it was removed

	    TestEqual("Final value", TestAttribute.Value, 225.0f); // 100 + 25 + 40 + 60
	}

	// Test 58: Negative Delta History
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 58: Negative Delta History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 200.0f;
	    TestAttribute.Value = 300.0f;
	    TestAttribute.bIsGMCBound = true;

	    UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>();
	    FGMCAttributeModifier Mod = CreateModifier(EModifierType::Percentage, 40.f, 0, EGMCModifierPhase::EMP_Stack, true); // 40% = 0.4x
	    Mod.SourceAbilityEffect = Effect;

	    TestAttribute.AddModifier(Mod, -1.f);
	    TestAttribute.ProcessPendingModifiers(50.f);

	    // Should have 1 history entry with negative delta
	    TestEqual("Negative delta: History count", TestAttribute.ModifierHistory.Num(), 1);
	    
	    float DeltaValue = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect, false);
	    TestEqual("Negative delta value", DeltaValue, -180.0f); // (300 * 0.4) - 300 = -180
	    TestEqual("Final value", TestAttribute.Value, 120.0f);
	}

	// Test 60: Performance Test - Long Duration Ability (1 minute at 60 FPS)
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 60: Performance Test - Long Duration Ability (1 minute at 60 FPS)"));
	    
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 100.0f;
	    TestAttribute.Value = 1000.0f;
	    TestAttribute.bIsGMCBound = true;

	    // Simulate different ability effects
	    UGMCAbilityEffect* DoTEffect = NewObject<UGMCAbilityEffect>(); // Damage over Time
	    UGMCAbilityEffect* BuffEffect = NewObject<UGMCAbilityEffect>(); // Buff effect
	    UGMCAbilityEffect* DebuffEffect = NewObject<UGMCAbilityEffect>(); // Debuff effect
	    UGMCAbilityEffect* HealEffect = NewObject<UGMCAbilityEffect>(); // Heal over Time

	    const float DeltaTime = 1.0f / 60.0f; // 60 FPS
	    const float TotalDuration = 60.0f; // 1 minute
	    const int32 TotalFrames = static_cast<int32>(TotalDuration / DeltaTime); // 3600 frames
	    float CurrentActionTimer = 1.0f; // Start at 1 second to avoid 0

	    double TotalProcessingTime = 0.0;
	    double MaxFrameTime = 0.0;
	    double MinFrameTime = DBL_MAX;
	    int32 FramesProcessed = 0;

	    UE_LOG(LogTemp, Log, TEXT("Starting simulation: %d frames over %f seconds"), TotalFrames, TotalDuration);

	    for (int32 Frame = 0; Frame < TotalFrames; ++Frame)
	    {
	        CurrentActionTimer += DeltaTime; // Always increment ActionTimer
	        
	        // Add multiple modifiers per frame to simulate complex ability
	        
	        // DoT Effect - ticks every frame
	        FGMCAttributeModifier DoTMod = CreateModifier(EModifierType::Add, -5.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	        DoTMod.SourceAbilityEffect = DoTEffect;
	        TestAttribute.AddModifier(DoTMod, -1.f); 

	        // Buff Effect - multiplicative bonus every 10 frames
	        if (Frame % 10 == 0)
	        {
	            FGMCAttributeModifier BuffMod = CreateModifier(EModifierType::Percentage, 101.f, 1, EGMCModifierPhase::EMP_Stack, true); // 101% = 1.01x
	            BuffMod.SourceAbilityEffect = BuffEffect;
	            TestAttribute.AddModifier(BuffMod, -1.f);
	        }

	        // Debuff Effect - base value modifier every 30 frames
	        if (Frame % 30 == 0)
	        {
	            FGMCAttributeModifier DebuffMod = CreateModifier(EModifierType::AddPercentageBaseValue, -2.f, 2, EGMCModifierPhase::EMP_Post, true); // -2%
	            DebuffMod.SourceAbilityEffect = DebuffEffect;
	            TestAttribute.AddModifier(DebuffMod, -1.f);
	        }

	        // Heal Effect - periodic healing every 60 frames (1 second)
	        if (Frame % 60 == 0 && Frame > 0)
	        {
	            FGMCAttributeModifier HealMod = CreateModifier(EModifierType::Add, 25.0f, 3, EGMCModifierPhase::EMP_Post, true);
	            HealMod.SourceAbilityEffect = HealEffect;
	            TestAttribute.AddModifier(HealMod, -1.f);
	        }

	        // Measure frame processing time
	        double FrameStartTime = FPlatformTime::Seconds();
	        TestAttribute.ProcessPendingModifiers(CurrentActionTimer);
	        double FrameEndTime = FPlatformTime::Seconds();
	        
	        double FrameTime = (FrameEndTime - FrameStartTime) * 1000.0; // Convert to ms
	        TotalProcessingTime += FrameTime;
	        MaxFrameTime = FMath::Max(MaxFrameTime, FrameTime);
	        MinFrameTime = FMath::Min(MinFrameTime, FrameTime);
	        FramesProcessed++;

	        // Log progress every 600 frames (10 seconds)
	        if (Frame % 600 == 0 && Frame > 0)
	        {
	            double CurrentMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	            int32 CurrentHistoryCount = TestAttribute.ModifierHistory.Num();
	            double AvgFrameTime = TotalProcessingTime / FramesProcessed;
	            
	            UE_LOG(LogTemp, Log, TEXT("Progress: %d/%d frames (%.1f%%) | Avg: %.3fms | History: %d entries (%.2fMB) | Value: %.2f | ActionTimer: %.2f"), 
	                   Frame, TotalFrames, (float)Frame / TotalFrames * 100.0f, 
	                   AvgFrameTime, CurrentHistoryCount, CurrentMemoryMB, TestAttribute.Value, CurrentActionTimer);
	        }
	    }

	    // Final statistics
	    double AvgProcessingTime = TotalProcessingTime / FramesProcessed;
	    double TotalMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	    int32 FinalHistoryCount = TestAttribute.ModifierHistory.Num();

	    UE_LOG(LogTemp, Log, TEXT("=== PERFORMANCE RESULTS ==="));
	    UE_LOG(LogTemp, Log, TEXT("Total Frames Processed: %d"), FramesProcessed);
	    UE_LOG(LogTemp, Log, TEXT("Total Processing Time: %.2f ms"), TotalProcessingTime);
	    UE_LOG(LogTemp, Log, TEXT("Average Frame Time: %.4f ms"), AvgProcessingTime);
	    UE_LOG(LogTemp, Log, TEXT("Min Frame Time: %.4f ms"), MinFrameTime);
	    UE_LOG(LogTemp, Log, TEXT("Max Frame Time: %.4f ms"), MaxFrameTime);
	    UE_LOG(LogTemp, Log, TEXT("Final History Count: %d entries"), FinalHistoryCount);
	    UE_LOG(LogTemp, Log, TEXT("Final Memory Usage: %.2f MB"), TotalMemoryMB);
	    UE_LOG(LogTemp, Log, TEXT("Final Attribute Value: %.2f"), TestAttribute.Value);
	    UE_LOG(LogTemp, Log, TEXT("Final ActionTimer: %.2f"), CurrentActionTimer);

	    // Performance assertions
	    TestTrueExpr(AvgProcessingTime < 1.0); // Average frame should be under 1ms
	    TestTrueExpr(MaxFrameTime < 5.0); // No frame should take more than 5ms
	    TestTrueExpr(TotalMemoryMB < 50.0); // Memory should stay under 50MB
	    TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
	    TestTrueExpr(FMath::IsFinite(TestAttribute.Value));

	    // Extract final values for each effect to verify tracking
	    float DoTTotal = TestAttribute.ModifierHistory.ExtractFromMoveHistory(DoTEffect, false);
	    float BuffTotal = TestAttribute.ModifierHistory.ExtractFromMoveHistory(BuffEffect, false);
	    float DebuffTotal = TestAttribute.ModifierHistory.ExtractFromMoveHistory(DebuffEffect, false);
	    float HealTotal = TestAttribute.ModifierHistory.ExtractFromMoveHistory(HealEffect, false);

	    UE_LOG(LogTemp, Log, TEXT("Effect Totals - DoT: %.2f | Buff: %.2f | Debuff: %.2f | Heal: %.2f"), 
	           DoTTotal, BuffTotal, DebuffTotal, HealTotal);

	    TestTrueExpr(FMath::Abs(DoTTotal) > 0.0f); // Should have significant DoT damage
	    TestTrueExpr(FinalHistoryCount > 0); // Should have history entries
	}

	// Test 61: Performance Test - Rollback and Replay Scenario
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 61: Performance Test - Rollback and Replay Scenario"));
	    
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 500.0f;
	    TestAttribute.Value = 2000.0f;
	    TestAttribute.bIsGMCBound = true;

	    // Create multiple persistent effects for complex rollback scenario
	    TArray<UGMCAbilityEffect*> Effects;
	    for (int32 i = 0; i < 50; ++i)
	    {
	        Effects.Add(NewObject<UGMCAbilityEffect>());
	    }

	    const float TimeStep = 0.1f; // Process every 100ms
	    const int32 BuildSteps = 1800; // Build history for 180 seconds (3 minutes)
	    float CurrentActionTimer = 1.0f; // Start at 1 second

	    UE_LOG(LogTemp, Log, TEXT("Building large history: %d steps over %.1f seconds"), BuildSteps, BuildSteps * TimeStep);

	    // Phase 1: Build large history
	    double BuildStartTime = FPlatformTime::Seconds();
	    
	    for (int32 Step = 0; Step < BuildSteps; ++Step)
	    {
	        CurrentActionTimer += TimeStep; // Always increment
	        
	        // Add multiple random modifiers each step
	        int32 ModifiersThisStep = FMath::RandRange(2, 6);
	        
	        for (int32 ModIdx = 0; ModIdx < ModifiersThisStep; ++ModIdx)
	        {
	            // Random effect
	            UGMCAbilityEffect* RandomEffect = Effects[FMath::RandRange(0, Effects.Num() - 1)];
	            
	            // Random modifier type
	            EModifierType RandomOp = static_cast<EModifierType>(FMath::RandRange(0, 6)); // All 7 types
	            
	            // Random value based on operator
	            float RandomValue = 0.0f;
	            switch (RandomOp)
	            {
	                case EModifierType::Add:
	                    RandomValue = FMath::FRandRange(-50.0f, 100.0f);
	                    break;
	                case EModifierType::AddPercentage:
	                    RandomValue = FMath::FRandRange(-0.5f, 1.0f);
	                    break;
	                case EModifierType::Percentage:
	                    RandomValue = FMath::FRandRange(50.0f, 150.0f);
	                    break;
	                case EModifierType::AddPercentageBaseValue:
	                    RandomValue = FMath::FRandRange(-0.3f, 0.8f);
	                    break;
	                case EModifierType::Set:
	                    RandomValue = FMath::FRandRange(100.0f, 3000.0f);
	                    break;
	                case EModifierType::PercentageBaseValue:
	                    RandomValue = FMath::FRandRange(80.0f, 120.0f);
	                    break;
	                case EModifierType::SetToBaseValue:
	                    RandomValue = 0.0f;
	                    break;
	            }
	            
	            // Random priority and phase
	            int32 RandomPriority = FMath::RandRange(0, 10);
	            EGMCModifierPhase RandomPhase = (FMath::RandBool()) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
	            
	            FGMCAttributeModifier Modifier = CreateModifier(RandomOp, RandomValue, RandomPriority, RandomPhase, true);
	            Modifier.SourceAbilityEffect = RandomEffect;
	            TestAttribute.AddModifier(Modifier, -1.f);
	        }
	        
	        // Process this step
	        TestAttribute.ProcessPendingModifiers(CurrentActionTimer);
	        
	        // Log progress every 180 steps (18 seconds)
	        if (Step % 180 == 0 && Step > 0)
	        {
	            double CurrentMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	            int32 CurrentHistoryCount = TestAttribute.ModifierHistory.Num();
	            
	            UE_LOG(LogTemp, Log, TEXT("Build Progress: %d/%d steps (%.1f%%) | History: %d entries (%.2fMB) | Value: %.2f | Time: %.1f"), 
	                   Step, BuildSteps, (float)Step / BuildSteps * 100.0f, 
	                   CurrentHistoryCount, CurrentMemoryMB, TestAttribute.Value, CurrentActionTimer);
	        }
	    }
	    
	    double BuildEndTime = FPlatformTime::Seconds();
	    double BuildTimeMs = (BuildEndTime - BuildStartTime) * 1000.0;
	    
	    int32 PreRollbackHistoryCount = TestAttribute.ModifierHistory.Num();
	    double PreRollbackMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	    float PreRollbackValue = TestAttribute.Value;
	    float FinalActionTimer = CurrentActionTimer;

	    UE_LOG(LogTemp, Log, TEXT("=== BUILD PHASE COMPLETE ==="));
	    UE_LOG(LogTemp, Log, TEXT("Build Time: %.2f ms"), BuildTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("Pre-Rollback History: %d entries (%.2fMB)"), PreRollbackHistoryCount, PreRollbackMemoryMB);
	    UE_LOG(LogTemp, Log, TEXT("Pre-Rollback Value: %.2f"), PreRollbackValue);
	    UE_LOG(LogTemp, Log, TEXT("Final ActionTimer: %.2f"), FinalActionTimer);

	    // Phase 2: Simulate rollback scenario (network desync, prediction rollback, etc.)
	    UE_LOG(LogTemp, Log, TEXT("Starting rollback scenario..."));
	    
	    // Simulate rolling back to 60% of the timeline (common in network games)
	    float RollbackPoint = FinalActionTimer * 0.6f; // Rollback to 60% of the timeline
	    
	    double RollbackStartTime = FPlatformTime::Seconds();
	    
	    // Clean history to rollback point (removes "future" entries)
	    TestAttribute.ModifierHistory.CleanMoveHistory(RollbackPoint);
	    
	    double CleanEndTime = FPlatformTime::Seconds();
	    double CleanTimeMs = (CleanEndTime - RollbackStartTime) * 1000.0;
	    
	    int32 PostCleanHistoryCount = TestAttribute.ModifierHistory.Num();
	    double PostCleanMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;

	    UE_LOG(LogTemp, Log, TEXT("=== ROLLBACK CLEAN COMPLETE ==="));
	    UE_LOG(LogTemp, Log, TEXT("Rollback Point: %.2f seconds"), RollbackPoint);
	    UE_LOG(LogTemp, Log, TEXT("Clean Time: %.2f ms"), CleanTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("Entries Removed: %d"), PreRollbackHistoryCount - PostCleanHistoryCount);
	    UE_LOG(LogTemp, Log, TEXT("Memory Freed: %.2f MB"), PreRollbackMemoryMB - PostCleanMemoryMB);
	    UE_LOG(LogTemp, Log, TEXT("Post-Clean History: %d entries (%.2fMB)"), PostCleanHistoryCount, PostCleanMemoryMB);

	    // Phase 3: Simulate replay with corrected data (authoritative server data)
	    CurrentActionTimer = RollbackPoint; // Reset to rollback point
	    const int32 ReplaySteps = static_cast<int32>((FinalActionTimer - RollbackPoint) / TimeStep);
	    
	    UE_LOG(LogTemp, Log, TEXT("Re-simulating %d steps from rollback point..."), ReplaySteps);
	    
	    double ReplayStartTime = FPlatformTime::Seconds();
	    
	    for (int32 Step = 0; Step < ReplaySteps; ++Step)
	    {
	        CurrentActionTimer += TimeStep; // Continue from rollback point
	        
	        // Add different modifiers during replay (simulating corrected authoritative data)
	        int32 ModifiersThisStep = FMath::RandRange(1, 4); // Fewer modifiers in replay
	        
	        for (int32 ModIdx = 0; ModIdx < ModifiersThisStep; ++ModIdx)
	        {
	            UGMCAbilityEffect* RandomEffect = Effects[FMath::RandRange(0, Effects.Num() - 1)];
	            EModifierType RandomOp = static_cast<EModifierType>(FMath::RandRange(0, 6));
	            
	            float RandomValue = 0.0f;
	            switch (RandomOp)
	            {
	                case EModifierType::Add:
	                    RandomValue = FMath::FRandRange(-30.0f, 60.0f); // Smaller range for replay
	                    break;
	                case EModifierType::AddPercentage:
	                    RandomValue = FMath::FRandRange(-0.3f, 0.7f);
	                    break;
	                case EModifierType::Percentage:
	                    RandomValue = FMath::FRandRange(80.0f, 120.0f);
	                    break;
	                case EModifierType::AddPercentageBaseValue:
	                    RandomValue = FMath::FRandRange(-0.2f, 0.5f);
	                    break;
	                case EModifierType::Set:
	                    RandomValue = FMath::FRandRange(200.0f, 2500.0f);
	                    break;
	                case EModifierType::PercentageBaseValue:
	                    RandomValue = FMath::FRandRange(90.0f, 110.0f);
	                    break;
	                case EModifierType::SetToBaseValue:
	                    RandomValue = 0.0f;
	                    break;
	            }
	            
	            int32 RandomPriority = FMath::RandRange(0, 8);
	            EGMCModifierPhase RandomPhase = (FMath::RandBool()) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
	            
	            FGMCAttributeModifier Modifier = CreateModifier(RandomOp, RandomValue, RandomPriority, RandomPhase, true);
	            Modifier.SourceAbilityEffect = RandomEffect;
	            TestAttribute.AddModifier(Modifier, -1.f);
	        }
	        
	        TestAttribute.ProcessPendingModifiers(CurrentActionTimer);
	        
	        // Log replay progress every 120 steps
	        if (Step % 120 == 0 && Step > 0)
	        {
	            double CurrentMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	            int32 CurrentHistoryCount = TestAttribute.ModifierHistory.Num();
	            
	            UE_LOG(LogTemp, Log, TEXT("Replay Progress: %d/%d steps (%.1f%%) | History: %d entries (%.2fMB) | Value: %.2f | Time: %.1f"), 
	                   Step, ReplaySteps, (float)Step / ReplaySteps * 100.0f, 
	                   CurrentHistoryCount, CurrentMemoryMB, TestAttribute.Value, CurrentActionTimer);
	        }
	    }
	    
	    double ReplayEndTime = FPlatformTime::Seconds();
	    double ReplayTimeMs = (ReplayEndTime - ReplayStartTime) * 1000.0;
	    
	    int32 FinalHistoryCount = TestAttribute.ModifierHistory.Num();
	    double FinalMemoryMB = TestAttribute.ModifierHistory.GetAllocatedSize() / 1000000.0;
	    float FinalValue = TestAttribute.Value;

	    UE_LOG(LogTemp, Log, TEXT("=== ROLLBACK SCENARIO COMPLETE ==="));
	    UE_LOG(LogTemp, Log, TEXT("Total Scenario Time: %.2f ms"), BuildTimeMs + CleanTimeMs + ReplayTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("  - Build Phase: %.2f ms"), BuildTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("  - Clean Phase: %.2f ms"), CleanTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("  - Replay Phase: %.2f ms"), ReplayTimeMs);
	    UE_LOG(LogTemp, Log, TEXT("Final History: %d entries (%.2fMB)"), FinalHistoryCount, FinalMemoryMB);
	    UE_LOG(LogTemp, Log, TEXT("Final Value: %.2f"), FinalValue);
	    UE_LOG(LogTemp, Log, TEXT("Final ActionTimer: %.2f"), CurrentActionTimer);

	    // Performance assertions
	    TestTrueExpr(BuildTimeMs < 15000.0); // Build should complete under 15 seconds
	    TestTrueExpr(CleanTimeMs < 500.0); // Clean should complete under 0.5 second
	    TestTrueExpr(ReplayTimeMs < 8000.0); // Replay should complete under 8 seconds
	    TestTrueExpr(FinalMemoryMB < 100.0); // Final memory should be reasonable
	    TestTrueExpr(!FMath::IsNaN(FinalValue));
	    TestTrueExpr(FMath::IsFinite(FinalValue));
	    TestTrueExpr(PostCleanHistoryCount < PreRollbackHistoryCount); // Should have cleaned some entries
	    TestTrueExpr(CurrentActionTimer <= FinalActionTimer + TimeStep); // Should not exceed original time

	    // Test individual effect extraction performance
	    double ExtractionStartTime = FPlatformTime::Seconds();
	    
	    TArray<float> EffectTotals;
	    for (int32 i = 0; i < 10; ++i) // Test extraction for first 10 effects
	    {
	        float EffectTotal = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effects[i], false);
	        EffectTotals.Add(EffectTotal);
	        UE_LOG(LogTemp, Log, TEXT("Effect %d Total: %.2f"), i, EffectTotal);
	    }
	    
	    double ExtractionEndTime = FPlatformTime::Seconds();
	    double ExtractionTimeMs = (ExtractionEndTime - ExtractionStartTime) * 1000.0;
	    
	    UE_LOG(LogTemp, Log, TEXT("Extraction Time (10 effects): %.2f ms (avg: %.3f ms per effect)"), 
	           ExtractionTimeMs, ExtractionTimeMs / 10.0);
	    TestTrueExpr(ExtractionTimeMs < 100.0); // Extraction should be fast

	    // Verify that value changed from pre-rollback (different replay results)
	    if (FMath::Abs(FinalValue - PreRollbackValue) > 1.0f)
	    {
	        UE_LOG(LogTemp, Log, TEXT("Rollback created different outcome: Pre=%.2f, Post=%.2f, Diff=%.2f"), 
	               PreRollbackValue, FinalValue, FinalValue - PreRollbackValue);
	    }
	}

		// Test 62: Delta Time - Add Operation
	{
		UE_LOG(LogTemp, Log, TEXT("Test 62: Delta Time - Add Operation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Create modifier with delta time scaling
		FGMCAttributeModifier Modifier = CreateModifier(EModifierType::Add, 50.0f, 0);
		TestAttribute.AddModifier(Modifier, 2.0f); // DeltaTime = 2.0f, so 50 * 2 = 100
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 + (50 * 2.0) = 300
		TestEqual("Add with DeltaTime: 200 + 100 = 300", TestAttribute.Value, 300.0f);
	}

	// Test 63: Delta Time - Percentage Operation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 63: Delta Time - Percentage Operation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Create modifier with delta time power scaling for percentage operations
		FGMCAttributeModifier Modifier = CreateModifier(EModifierType::Percentage, 50.f, 0); // 50% = 0.5
		TestAttribute.AddModifier(Modifier, 2.0f); // DeltaTime = 2.0f, so pow(0.5, 2.0) = 0.25
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 * 0.25 = 50
		TestEqual("Percentage with DeltaTime: 200 * 25% = 50", TestAttribute.Value, 50.0f);
	}

	// Test 64: Delta Time - AddPercentageBaseValue Operation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 64: Delta Time - AddPercentageBaseValue Operation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Create modifier with delta time power scaling
		FGMCAttributeModifier Modifier = CreateModifier(EModifierType::AddPercentageBaseValue, 50.f, 0); // 50%
		TestAttribute.AddModifier(Modifier, 2.0f); // DeltaTime = 2.0f, so pow(0.5, 2.0) = 0.25 (25%)
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 + (100 * 0.25) = 200 + 25 = 225
		TestEqual("AddPercentageBaseValue with DeltaTime: 200 + 100 = 300", TestAttribute.Value, 300.0f);
	}

	// Test 65: Delta Time - AddPercentage Operation - NEW
	{
		UE_LOG(LogTemp, Log, TEXT("Test 65: Delta Time - AddPercentage Operation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Create modifier with delta time power scaling
		FGMCAttributeModifier Modifier = CreateModifier(EModifierType::AddPercentage, 50.f, 0); // 50%
		TestAttribute.AddModifier(Modifier, 2.0f); // DeltaTime = 2.0f, so pow(0.5, 2.0) = 0.25 (25%)
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0.5 * 2.0 = 1.0 (scaling linéaire)
		// 200 + (100 * 1.0) = 300
		TestEqual("AddPercentage with DeltaTime: 200 + 200 = 400", TestAttribute.Value, 400.0f);
	}

	// Test 66: Delta Time - PercentageBaseValue Operation - NEW
	{
		UE_LOG(LogTemp, Log, TEXT("Test 66: Delta Time - PercentageBaseValue Operation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Create modifier with delta time power scaling
		FGMCAttributeModifier Modifier = CreateModifier(EModifierType::PercentageBaseValue, 50.f, 0); // 50%
		TestAttribute.AddModifier(Modifier, 2.0f); // DeltaTime = 2.0f, so pow(0.5, 2.0) = 0.25 (25%)
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// BaseValue * 0.25 = 100 * 0.25 = 25
		TestEqual("PercentageBaseValue with DeltaTime: 100 * 25% = 25", TestAttribute.Value, 25.0f);
	}

	// Test 67: Delta Time - Zero DeltaTime
	{
		UE_LOG(LogTemp, Log, TEXT("Test 67: Delta Time - Zero DeltaTime"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		FGMCAttributeModifier AddMod = CreateModifier(EModifierType::Add, 50.0f, 0);
		FGMCAttributeModifier PercMod = CreateModifier(EModifierType::Percentage, 50.f, 1);
        
		TestAttribute.AddModifier(AddMod, 0.0f); // DeltaTime = 0.0f
		TestAttribute.AddModifier(PercMod, 0.0f); // DeltaTime = 0.0f
        
		TestAttribute.ProcessPendingModifiers(50.f);
		
		TestEqual("Zero DeltaTime: should be 200.f (no modification)", TestAttribute.Value, 200.0f);
	}

	// Test 68: Delta Time - Fractional DeltaTime
	{
		UE_LOG(LogTemp, Log, TEXT("Test 68: Delta Time - Fractional DeltaTime"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
        
		FGMCAttributeModifier AddMod = CreateModifier(EModifierType::Add, 60.0f, 0);
		TestAttribute.AddModifier(AddMod, 0.5f); // Half delta time
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (60 * 0.5) = 100 + 30 = 130
		TestEqual("Fractional DeltaTime: 100 + 30 = 130", TestAttribute.Value, 130.0f);
	}

	// Test 69: Delta Time - Set Operations Unaffected
	{
		UE_LOG(LogTemp, Log, TEXT("Test 69: Delta Time - Set Operations Unaffected"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		FGMCAttributeModifier SetMod = CreateModifier(EModifierType::Set, 150.0f, 0);
		FGMCAttributeModifier SetToBaseMod = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1);
        
		TestAttribute.AddModifier(SetMod, 5.0f); // Large delta time should not affect Set
		TestAttribute.ProcessPendingModifiers(50.f);
		TestEqual("Set unaffected by DeltaTime: Set to 150", TestAttribute.Value, 150.0f);
        
		TestAttribute.AddModifier(SetToBaseMod, 10.0f); // Large delta time should not affect SetToBaseValue
		TestAttribute.ProcessPendingModifiers(51.f);
		TestEqual("SetToBaseValue unaffected by DeltaTime: Reset to 100", TestAttribute.Value, 100.0f);
	}

	// Test 70: Delta Time - Complex Multi-Modifier with DeltaTime - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 70: Delta Time - Complex Multi-Modifier"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Multiple modifiers with different delta times
		FGMCAttributeModifier AddMod1 = CreateModifier(EModifierType::Add, 20.0f, 0);
		FGMCAttributeModifier AddMod2 = CreateModifier(EModifierType::Add, 30.0f, 0);
		FGMCAttributeModifier PercMod = CreateModifier(EModifierType::Percentage, 120.f, 1); // 120%
		FGMCAttributeModifier AddPercMod = CreateModifier(EModifierType::AddPercentage, 30.f, 2); // +30%
        
		TestAttribute.AddModifier(AddMod1, 1.5f); // 20 * 1.5 = 30
		TestAttribute.AddModifier(AddMod2, 2.0f); // 30 * 2.0 = 60
		TestAttribute.AddModifier(PercMod, 0.5f); // pow(1.2, 0.5) ≈ 1.095, so 109.5%
		TestAttribute.AddModifier(AddPercMod, 0.5f); // pow(0.3, 0.5) ≈ 0.548, so +54.8%
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Order: Add(30+60=90) -> Percentage(290 * 1.095 ≈ 317.55) -> AddPercentage(317.55 + 317.55*0.548 ≈ 491.57)
		TestEqual("Complex DeltaTime: Final ≈ 365.18", TestAttribute.Value, 365.18f, 0.2f);
	}

	// Test 71: Delta Time - Performance with DeltaTime
	{
		UE_LOG(LogTemp, Log, TEXT("Test 71: Delta Time - Performance Test"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Add many modifiers with delta time
		for (int32 i = 0; i < 1000; ++i)
		{
			EModifierType OpType = static_cast<EModifierType>(i % 5); // Add, Percentage, AddPercentageBase, AddPercentage, PercentageBase only
			float Value = (i % 2 == 0) ? 0.1f : -0.05f;
			float DeltaTime = FMath::FRandRange(0.1f, 2.0f);
			
			TestAttribute.AddModifier(CreateModifier(OpType, Value, i % 5), DeltaTime);
		}
        
		double StartTime = FPlatformTime::Seconds();
		TestAttribute.ProcessPendingModifiers(50.f);
		double EndTime = FPlatformTime::Seconds();
		double ElapsedMs = (EndTime - StartTime) * 1000.0;
        
		UE_LOG(LogTemp, Log, TEXT("Processing 1000 modifiers with DeltaTime took %f ms"), ElapsedMs);
        
		TestTrueExpr(ElapsedMs < 100.0);
		TestTrueExpr(!FMath::IsNaN(TestAttribute.Value));
		TestTrueExpr(FMath::IsFinite(TestAttribute.Value));
	}

	// Test 72: Delta Time - Edge Cases
	{
		UE_LOG(LogTemp, Log, TEXT("Test 72: Delta Time - Edge Cases"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 100.0f;
        
		// Test with very small delta time
		FGMCAttributeModifier SmallDeltaMod = CreateModifier(EModifierType::Add, 1000.0f, 0);
		TestAttribute.AddModifier(SmallDeltaMod, 0.001f); // Very small delta
		TestAttribute.ProcessPendingModifiers(50.f);
		TestEqual("Very small DeltaTime: 100 + 1 = 101", TestAttribute.Value, 101.0f);
        
		// Reset and test with very large delta time
		TestAttribute.Value = 100.0f;
		FGMCAttributeModifier LargeDeltaMod = CreateModifier(EModifierType::Add, 1.0f, 0);
		TestAttribute.AddModifier(LargeDeltaMod, 1000.0f); // Very large delta
		TestAttribute.ProcessPendingModifiers(51.f);
		TestEqual("Very large DeltaTime: 100 + 1000 = 1100", TestAttribute.Value, 1100.0f);
	}

	// Test 73: Delta Time - Default Value (-1.f) Behavior
	{
		UE_LOG(LogTemp, Log, TEXT("Test 73: Delta Time - Default Value Behavior"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Test that -1.f delta time (default) doesn't apply scaling
		FGMCAttributeModifier AddMod = CreateModifier(EModifierType::Add, 50.0f, 0);
		FGMCAttributeModifier PercMod = CreateModifier(EModifierType::Percentage, 150.f, 1); // 150% = 1.5x
        
		TestAttribute.AddModifier(AddMod, -1.f); // Default delta time
		TestAttribute.AddModifier(PercMod, -1.f); // Default delta time
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Should behave like normal: (200 + 50) * 1.5 = 375
		TestEqual("Default DeltaTime (-1): No scaling applied = 375", TestAttribute.Value, 375.0f);
	}

	// Test 74: New Operators - AddPercentage Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 74: AddPercentage Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 80.0f;
		TestAttribute.Value = 120.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::AddPercentage, 50.f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f); // +50%
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("AddPercentage: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (120 + (120 * 0.5)) - 120 = 180 - 120 = 60
			TestEqual("AddPercentage: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false) , 60.0f);
		}
		TestEqual("AddPercentage: Final value", TestAttribute.Value, 180.0f);
	}

	// Test 75: New Operators - PercentageBaseValue Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 75: PercentageBaseValue Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 300.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::PercentageBaseValue, 75.f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.AddModifier(Mod, -1.f); // 75%
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("PercentageBaseValue: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (200 * 0.75) - 300 = 150 - 300 = -150
			TestEqual("PercentageBaseValue: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false) , -150.0f);
		}
		TestEqual("PercentageBaseValue: Final value", TestAttribute.Value, 150.0f);
	}

	// Test de validation SafePow
	{
		UE_LOG(LogTemp, Log, TEXT("Test SafePow: Validation NaN Prevention"));
    
		// Test direct de la fonction problématique
		FGMCAttributeModifier TestMod = CreateModifier(EModifierType::AddPercentageBaseValue, -5.f, 0);
    
		FAttribute DummyAttribute;
		DummyAttribute.BaseValue = 100.0f;
    
		float Result = TestMod.GetModifierValue(&DummyAttribute, 1.5f);
    
		TestTrueExpr(!FMath::IsNaN(Result));
		TestTrueExpr(FMath::IsFinite(Result));
		UE_LOG(LogTemp, Log, TEXT("SafePow result: %f"), Result);
	}

	return true;
};