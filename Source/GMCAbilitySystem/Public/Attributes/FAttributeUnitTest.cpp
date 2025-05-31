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
    Modifier.Value = Value;
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0));
    	
		TestAttribute.ProcessPendingModifiers(50.f);
        
		TestEqual("SetToBaseValue: Should reset to BaseValue (150)", TestAttribute.Value, 150.0f);
	}

	// Test 2: AddMultiplyBaseValue basic functionality - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 2: AddMultiplyBaseValue Basic Functionality"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		// Add 50% of BaseValue (0.5 bonus = 1.5x multiplier)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.5f, 0));
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 200 + (100 * 1.5) = 200 + 150 = 350
		TestEqual("AddMultiplyBaseValue: 200 + (100 * 1.5) = 350", TestAttribute.Value, 350.0f);
	}

	// Test 3: Complete operator priority order verification - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 3: Operator Priority Order (SetToBaseValue->Set->AddMultiplyBaseValue->Multiply->Add)"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		// Add in reverse order to test sorting
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 20.0f, 0));           // Should be last
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 0));      // Should be 4th (+50% = 1.5x)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.6f, 0)); // Should be 3rd (+60% = 1.6x)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 80.0f, 0));          // Should be 2nd
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0)); // Should be 1st
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Order: SetToBaseValue(50) -> Set(80) -> AddMultiplyBaseValue(80 + 50*1.6 = 160) -> Multiply(160 * 1.5 = 240) -> Add(240 + 20 = 260)
		TestEqual("Priority order: ((((50 =80) +80) *1.5) +20) = 260", TestAttribute.Value, 260.0f);
	}

	// Test 4: AddMultiplyBaseValue agglomeration - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 4: AddMultiplyBaseValue Agglomeration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 100.0f;
        
		// Multiple AddMultiplyBaseValue with same priority should agglomerate
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.25f, 0, EGMCModifierPhase::EMP_Stack)); // +25% = 1.25x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.15f, 0, EGMCModifierPhase::EMP_Stack)); // +15% = 1.15x  
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.1f, 0, EGMCModifierPhase::EMP_Post));  // +10% = 1.1x, different phase
        
        
		TestAttribute.ProcessPendingModifiers(50);
        
		// Phase EMP_Stack: CurModVal = 1 + 0.25 + 0.15 = 1.4, so 100 + (200 * 1.4) = 100 + 280 = 380
		// Phase EMP_Post: 380 + (200 * 1.1) = 380 + 220 = 600
		TestEqual("AddMultiplyBaseValue agglomeration: 100 + 280 + 220 = 600", TestAttribute.Value, 600.0f);
	}

	// Test 5: SetToBaseValue with different priorities and phases
	{
		UE_LOG(LogTemp, Log, TEXT("Test 5: Multiple SetToBaseValue Operations"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 75.0f;
		TestAttribute.Value = 150.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack)); // Executed in first
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Post)); // Executed in third
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 25.0f, 3, EGMCModifierPhase::EMP_Stack)); // Executed in second
        
        
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 0, EGMCModifierPhase::EMP_Stack));  // +50% = 1.5x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.25f, 0, EGMCModifierPhase::EMP_Stack)); // +25% = 1.25x -> combined = 1.75x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 30.0f, 1, EGMCModifierPhase::EMP_Post));     // +30 flat
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 15.0f, 1, EGMCModifierPhase::EMP_Post));     // +15 flat
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 120 * (1 + 0.5 + 0.25) = 120 * 1.75 = 210
		// Priority 1: 210 + (30 + 15) = 210 + 45 = 255
		TestEqual("Multiply then Add: (120 * 1.75) + 45 = 255", TestAttribute.Value, 255.0f);
	}

	// Test 7: Zero BaseValue with AddMultiplyBaseValue
	{
		UE_LOG(LogTemp, Log, TEXT("Test 7: Zero BaseValue with AddMultiplyBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 2.0f, 0)); // +200% of 0
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (0 * 3.0) = 100 + 0 = 100
		TestEqual("Zero BaseValue: 100 + (0 * 3.0) = 100", TestAttribute.Value, 100.0f);
	}

	// Test 8: Negative AddMultiplyBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 8: Negative AddMultiplyBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 200.0f;
		TestAttribute.Value = 300.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, -0.4f, 0, EGMCModifierPhase::EMP_Stack)); // -40% = 0.6x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 300 + (200 * (1 + (-0.4))) = 300 + (200 * 0.6) = 300 + 120 = 420
		TestEqual("Negative AddMultiplyBaseValue: 300 + (200 * 0.6) = 420", TestAttribute.Value, 420.0f);
	}

	// Test 9: Set operation overriding everything
	{
		UE_LOG(LogTemp, Log, TEXT("Test 9: Set Operation Override"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 250.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 500.0f, 1, EGMCModifierPhase::EMP_Post));      // Set to 500
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 1000.0f, 0, EGMCModifierPhase::EMP_Stack));    // This happens first but gets overridden
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 250 + 1000 = 1250, Priority 1: Set to 500
		TestEqual("Set override: Add first, then Set = 500", TestAttribute.Value, 500.0f);
	}

	// Test 10: Extreme AddMultiplyBaseValue values - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 10: Extreme AddMultiplyBaseValue Values"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 10.0f, 0)); // +1000% = 11x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 100 + (50 * 11.0) = 100 + 550 = 650
		TestEqual("Extreme AddMultiplyBaseValue: 100 + 550 = 650", TestAttribute.Value, 650.0f);
	}

	// Test 11: All operators in single group (same priority) - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 11: All Operators Same Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 40.0f;
		TestAttribute.Value = 80.0f;
        
		// All same priority, should execute in operator order
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 10.0f, 0));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 0)); // +50% = 1.5x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.25f, 0)); // +25% = 1.25x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 60.0f, 0));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0));
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// SetToBaseValue(40) -> Set(60) -> AddMultiplyBaseValue(60+(40*1.25)=110) -> Multiply(110*1.5=165) -> Add(165+10=175)
		TestEqual("All operators same priority: Final result = 175", TestAttribute.Value, 175.0f);
	}

	// Test 12: Priority gaps with new operators - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 12: Priority Gaps with New Operators"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 25.0f;
		TestAttribute.Value = 75.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 2.0f, 100, EGMCModifierPhase::EMP_Post));  // Very high priority +200% = 3x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack));         // Low priority
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 1.0f, 50, EGMCModifierPhase::EMP_Stack));             // Medium priority +100% = 2x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 1: Reset to 25, Priority 50: 25*2=50, Priority 100: 50+(25*3)=125
		TestEqual("Priority gaps: Reset->Multiply->AddMultiplyBase = 125", TestAttribute.Value, 125.0f);
	}

	// Test 13: Floating point precision with AddMultiplyBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 13: Floating Point Precision"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 33.333333f;
		TestAttribute.Value = 66.666666f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.333333f, 0));
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 66.666666 + (33.333333 * 1.333333) ≈ 66.666666 + 44.444444 ≈ 111.111111
		TestTrueExpr(FMath::IsNearlyEqual(TestAttribute.Value, 111.111111f, 0.001f));
	}

	// Test 14: Multiple SetToBaseValue operations
	{
		UE_LOG(LogTemp, Log, TEXT("Test 14: Multiple SetToBaseValue Same Priority"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 500.0f;
        
		// Multiple SetToBaseValue in same priority group
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack));
        
        
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 150.0f, 0, EGMCModifierPhase::EMP_Stack));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack)); // Same priority, but SetToBaseValue comes first in enum
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// SetToBaseValue (50) then Set (150) in same priority group
		TestEqual("SetToBaseValue then Set: Final = 150", TestAttribute.Value, 150.0f);
	}

	// Test 16: Complex chain with all operators - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 16: Complex Chain All Operators"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 20.0f;
		TestAttribute.Value = 100.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack));        // Reset to 20
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 40.0f, 1, EGMCModifierPhase::EMP_Stack));                 // Set to 40
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 1.5f, 2, EGMCModifierPhase::EMP_Post)); // Add 50 (20*2.5)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.6f, 3, EGMCModifierPhase::EMP_Stack));             // Multiply by 1.6
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 18.0f, 4, EGMCModifierPhase::EMP_Post));                 // Add 18
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 20 -> Set(40) -> Multiply(40 * 1.6 = 64) -> AddMultiplyBaseValue(64 + (20 * 2.5) = 64 + 50 = 114) -> Add(114 + 18 = 132)
		TestEqual("All operators chain: Final = 132", TestAttribute.Value, 132.0f);
	}

	// Test 17: AddMultiplyBaseValue with zero current value - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 17: AddMultiplyBaseValue with Zero Current Value"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 80.0f;
		TestAttribute.Value = 0.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.75f, 0)); // +75% = 1.75x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0 + (80 * 1.75) = 0 + 140 = 140
		TestEqual("Zero current + AddMultiplyBase: 0 + 140 = 140", TestAttribute.Value, 140.0f);
	}

	// Test 18: Massive AddMultiplyBaseValue accumulation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 18: Massive AddMultiplyBaseValue Accumulation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 10.0f;
		TestAttribute.Value = 50.0f;
        
		// Add 10 modifiers of 20% each
		for (int32 i = 0; i < 10; ++i)
		{
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.2f, 0));
		}
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// CurModVal = 1 + (10 * 0.2) = 1 + 2 = 3, so 50 + (10 * 3) = 50 + 30 = 80
		TestEqual("10x AddMultiplyBase: 50 + 30 = 80", TestAttribute.Value, 80.0f);
	}

	// Test 19: Negative BaseValue scenarios - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 19: Negative BaseValue Scenarios"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = -30.0f;
		TestAttribute.Value = 60.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 2.0f, 0, EGMCModifierPhase::EMP_Stack));   // +200% = 3x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Post));        // Reset to -30
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Priority 0: 60 + (-30 * 3) = 60 - 90 = -30, Priority 1: Reset to -30
		TestEqual("Negative BaseValue: Reset to -30", TestAttribute.Value, -30.0f);
	}

	// Test 20: Mixed positive and negative AddMultiplyBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 20: Mixed Positive/Negative AddMultiplyBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.5f, 0, EGMCModifierPhase::EMP_Stack));  // +50%
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, -0.3f, 0, EGMCModifierPhase::EMP_Stack)); // -30%
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.1f, 0, EGMCModifierPhase::EMP_Stack));  // +10%
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// CurModVal = 1 + 0.5 - 0.3 + 0.1 = 1.3, so 200 + (100 * 1.3) = 200 + 130 = 330
		TestEqual("Mixed +/- AddMultiplyBase: 200 + 130 = 330", TestAttribute.Value, 330.0f);
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 25.0f, 999999, EGMCModifierPhase::EMP_Post));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 1.0f, 500000, EGMCModifierPhase::EMP_Stack)); // +100% = 2x
        
        
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 5.0f, 0, EGMCModifierPhase::EMP_Stack));      // +500% = 6x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 1000.0f, 1, EGMCModifierPhase::EMP_Post));       // Make it massive
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Post)); // Reset everything
        
        
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
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.777f, 0, EGMCModifierPhase::EMP_Stack)); // +77.7% = 1.777x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.333f, 1, EGMCModifierPhase::EMP_Post)); // +33.3% = 1.333x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Step 1: 15.555 + (7.777 * 1.777) = 15.555 + 13.819729 = 29,374729 
		// Step 2: 29,374729 * 1.333 ≈ 39,156513757
		TestEqual("Float Precision Test", TestAttribute.Value, 39.156513757f, 0.000000001f);
	}

	// Test 25: All operations with history tracking
	{
		UE_LOG(LogTemp, Log, TEXT("Test 25: All Operations with History Tracking"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 40.0f;
		TestAttribute.Value = 80.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.5f, 1, EGMCModifierPhase::EMP_Post, true)); // +50% = 1.5x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 15.0f, 2, EGMCModifierPhase::EMP_Stack, false)); // Not tracked
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Should have 2 history entries (SetToBaseValue and AddMultiplyBaseValue)
		TestEqual("History tracking: 2 entries", TestAttribute.ModifierHistory.Num(), 2);
		TestEqual("Final value: 40 -> 100 -> 115", TestAttribute.Value, 115.0f);
	}

	// Test 26: Extreme multiplication with AddMultiplyBaseValue - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 26: Extreme Multiplication with AddMultiplyBaseValue"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 5.0f;
		TestAttribute.Value = 10.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 100.0f, 0, EGMCModifierPhase::EMP_Stack)); // +10000% = 101x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 2.0f, 1, EGMCModifierPhase::EMP_Post));              // +200% = 3x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// Step 1: 10 + (5 * 101) = 10 + 505 = 515
		// Step 2: 515 * 3 = 1545
		TestEqual("Extreme operations: Final = 1545", TestAttribute.Value, 1545.0f);
	}

	// Test 27: Zero value propagation - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 27: Zero Value Propagation"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.0f;
		TestAttribute.Value = 0.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 5.0f, 0, EGMCModifierPhase::EMP_Stack)); // +500% = 6x of 0
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 10.0f, 1, EGMCModifierPhase::EMP_Stack));           // +1000% = 11x of 0
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 50.0f, 2, EGMCModifierPhase::EMP_Post));               // Finally non-zero
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0 + 0 = 0, 0 * 11 = 0, 0 + 50 = 50
		TestEqual("Zero propagation: Final = 50", TestAttribute.Value, 50.0f);
	}

	// Test 28: Alternating SetToBaseValue and modifications - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 28: Alternating SetToBaseValue and Modifications"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 30.0f;
		TestAttribute.Value = 120.0f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 20.0f, 0, EGMCModifierPhase::EMP_Stack)); // First Operation
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack)); // Second Operation
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 2.0f, 2, EGMCModifierPhase::EMP_Post)); // Third Operation
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 3, EGMCModifierPhase::EMP_Post));
        
        
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
			EModifierType OpType = static_cast<EModifierType>(i % 5);
			int32 Priority = i % 20;
			float Value = (i % 3 == 0) ? 0.1f : -0.05f;
			EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
			TestAttribute.PendingModifiers.Add(CreateModifier(OpType, Value, Priority, Phase));
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
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;
		TestAttributeBound.bIsGMCBound = true;
    	 
        
		// Add 50000 modifiers of various types with mixed phases
		for (int32 i = 0; i < 50000; ++i)
		{
			EModifierType OpType = static_cast<EModifierType>(i % 5);
			int32 Priority = i % 20;
			float Value = (i % 3 == 0) ? 0.1f : -0.05f;
			EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
			if (i % 2 == 0)
			{
				TestAttribute.PendingModifiers.Add(CreateModifier(OpType, Value, Priority, Phase, true));
			}
			else
			{
				TestAttributeBound.PendingModifiers.Add(CreateModifier(OpType, Value, Priority, Phase, true));
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
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 5, EGMCModifierPhase::EMP_Stack));              // +50% = 1.5x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 25.0f, 10, EGMCModifierPhase::EMP_Post));       // +25
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack));        // Reset to 100
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.3f, 7, EGMCModifierPhase::EMP_Stack));  // +30% = 1.3x
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 80.0f, 3, EGMCModifierPhase::EMP_Post));        // Set to 80
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.2f, 7, EGMCModifierPhase::EMP_Stack));  // +20% = 1.2x (same priority/phase)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 1.0f, 5, EGMCModifierPhase::EMP_Stack));              // +100% = 2x (same priority/phase)
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 15.0f, 10, EGMCModifierPhase::EMP_Post));       // +15 (same priority/phase)
	    
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// PHASE EMP_Stack (Phase 0) - All processed first regardless of priority:
		//   Priority 1: SetToBaseValue → Reset to 100
		//   Priority 5: Multiply(0.5f) + Multiply(1.0f) → CurModVal = 1 + 0.5 + 1.0 = 2.5 → 100 * 2.5 = 250
		//   Priority 7: AddMultiplyBaseValue(0.3f) + AddMultiplyBaseValue(0.2f) → CurModVal = 1 + 0.3 + 0.2 = 1.5 → 250 + (100 * 1.5) = 400
		//
		// PHASE EMP_PostCalculation (Phase 1) - All processed after EMP_Stack:
		//   Priority 3: Set to 80 → 80
		//   Priority 10: Add(25.0f) + Add(15.0f) → CurModVal = 25 + 15 = 40 → 80 + 40 = 120
	    
		TestEqual("Ultra complex with phase priority: Final = 120", TestAttribute.Value, 120.0f);
	}

	// Test 31: Bonus - Edge case with very small numbers - CORRECTED
	{
		UE_LOG(LogTemp, Log, TEXT("Test 31: Edge Case - Very Small Numbers"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 0.001f;
		TestAttribute.Value = 0.002f;
        
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 1000.0f, 0, EGMCModifierPhase::EMP_Stack)); // +100000% = 1001x
        
        
		TestAttribute.ProcessPendingModifiers(50.f);
        
		// 0.002 + (0.001 * 1001) = 0.002 + 1.001 = 1.003
		TestEqual("Very small numbers: Final = 1.003", TestAttribute.Value, 1.003f);
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
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 1.0f, Priority, Phase));
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.1f, Priority, Phase)); // +10% = 1.1x
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.1f, Priority, Phase)); // +10% = 1.1x
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 15.0f + Priority, Priority, Phase));
			TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, Priority, Phase));
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
		TestAttribute.PendingModifiers.Add(Mod);
	    
	    
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
		TestAttribute.PendingModifiers.Add(Mod);
	    
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
	    
		TestEqual("Set: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: 300 (new value) - 200 = 100
			TestEqual("Set: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false), 100.f);
		}
		TestEqual("Set: Final value", TestAttribute.Value, 300.0f);
	}

	// Test 35: AddMultiplyBaseValue - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 35: AddMultiplyBaseValue Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 50.0f;
		TestAttribute.Value = 100.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::AddMultiplyBaseValue, 0.6f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.PendingModifiers.Add(Mod); // +60% = 1.6x
	    
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("AddMultiplyBaseValue: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (100 + (50 * 1.6)) - 100 = 180 - 100 = 80
			TestEqual("AddMultiplyBaseValue: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false), 80.0f);
		}
		TestEqual("AddMultiplyBaseValue: Final value", TestAttribute.Value, 180.0f);
	}

	// Test 36: Multiply - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 36: Multiply Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 80.0f;
		TestAttribute.Value = 120.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Multiply, 0.5f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.PendingModifiers.Add(Mod); // +50% = 1.5x
	    
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 1 history entry
		TestEqual("Multiply: History count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() > 0)
		{
			// Delta should be: (120 * 1.5) - 120 = 180 - 120 = 60
			TestEqual("Multiply: Delta value", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod.SourceAbilityEffect.Get(), false) , 60.0f);
		}
		TestEqual("Multiply: Final value", TestAttribute.Value, 180.0f);
	}

	// Test 37: Add - Individual History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 37: Add Individual History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 90.0f;
		TestAttribute.Value = 110.0f;

		FGMCAttributeModifier Mod = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.PendingModifiers.Add(Mod);
	    
	    
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

	// Test 38: Multiple Operators - Selective History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 38: Multiple Operators - Selective History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 100.0f;
		TestAttribute.Value = 200.0f;
		TestAttribute.bIsGMCBound = true;

		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true); // Tracked 200 + 50 = 250
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Multiply, 0.5f, 1, EGMCModifierPhase::EMP_Stack, false); // Not tracked // 250 * 1.5 = 375
		Mod2.SourceAbilityEffect = Mod1.SourceAbilityEffect;
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 400.0f, 2, EGMCModifierPhase::EMP_Post, true); // Tracked  // 375 -> 400 
		Mod3.SourceAbilityEffect = Mod1.SourceAbilityEffect;
    	
		TestAttribute.PendingModifiers.Add(Mod1);      // Tracked
		TestAttribute.PendingModifiers.Add(Mod2); // Not tracked
		TestAttribute.ProcessPendingModifiers(50.f);

		// Test first register
		TestEqual("Selective history: Entry count", TestAttribute.ModifierHistory.Num(), 1);
		if (TestAttribute.ModifierHistory.Num() >= 1)
		{
			// First entry: Add 50 to 200 = 250, delta = 50
			TestEqual("First entry delta (Add)", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod1.SourceAbilityEffect.Get(), true), 50.0f);
		}
    	
		TestAttribute.PendingModifiers.Add(Mod3);    // Tracked
		TestAttribute.ProcessPendingModifiers(60.f);
    	
		// Should have 2 history entries (Add and Set, not Multiply)
		TestEqual("Selective history: Entry count", TestAttribute.ModifierHistory.Num(), 1);
    	
		if (TestAttribute.ModifierHistory.Num() >= 1)
		{
			// Second entry: Set to 400 from 375 (250 * 1.5), delta = 25
			TestEqual("First entry delta (Add)", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod1.SourceAbilityEffect.Get(), false) , 25.0f);
		}
		TestEqual("Final value", TestAttribute.Value, 400.0f);
	}

	// Test 39: Cross-Phase History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 39: Cross-Phase History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 60.0f;
		TestAttribute.Value = 120.0f;
		TestAttribute.bIsGMCBound = true;

    	
		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::AddMultiplyBaseValue, 0.5f, 0, EGMCModifierPhase::EMP_Stack, true); // +50% = 1.5x

    	
		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 30.0f, 1, EGMCModifierPhase::EMP_Post, true); // Tracked
		Mod3.SourceAbilityEffect = Mod2.SourceAbilityEffect;
    	
		TestAttribute.PendingModifiers.Add(Mod2); // +50% = 1.5x
		TestAttribute.ProcessPendingModifiers(50.f);
		TestAttribute.PendingModifiers.Add(Mod3);
		TestAttribute.ProcessPendingModifiers(60.f);
	    
		// Should have 2 history entries
		TestEqual("Cross-phase history: Entry count", TestAttribute.ModifierHistory.Num(), 2);
	    
		if (TestAttribute.ModifierHistory.Num() >= 2)
		{
			TestEqual("Post phase delta", TestAttribute.ModifierHistory.ExtractFromMoveHistory(Mod3.SourceAbilityEffect.Get(), true), 120.0f);
		}
		TestEqual("Final value", TestAttribute.Value, 240.0f);
	}

	// Test 40: Zero Delta - No History Registration
	{
		UE_LOG(LogTemp, Log, TEXT("Test 40: Zero Delta - No History Registration"));
		FAttribute TestAttribute;
		TestAttribute.BaseValue = 150.0f;
		TestAttribute.Value = 150.0f; // Same as base
	    
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true));
	    
	    
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
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true));  
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 15.0f, 0, EGMCModifierPhase::EMP_Stack, true));
	    
	    
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

		FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::Multiply, -0.6f, 0, EGMCModifierPhase::EMP_Stack, true);
		TestAttribute.PendingModifiers.Add(Mod1); // -60% = 0.4x
	    
	    
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
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 1.0f, 1, EGMCModifierPhase::EMP_Stack, true)); // +100% = 2x
	    
		// EMP_Post phase  
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true));
		TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 4, EGMCModifierPhase::EMP_Post, true)); // +50% = 1.5x
	    
	    
		TestAttribute.ProcessPendingModifiers(50.f);
	    
		// Should have 4 history entries
		TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);
	    
		// Execution order due to Phase > Priority:
		// EMP_Stack Priority 1: AddMultiplyBaseValue first
		// EMP_Stack Priority 2: SetToBaseValue second
		// EMP_Post Priority 3: Set
		// EMP_Post Priority 4: Multiply
	    
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
		BoundAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true));
		BoundAttribute.ProcessPendingModifiers(10.0f);
		TestEqual("Bound: History count", BoundAttribute.ModifierHistory.Num(), 1);
		TestEqual("Bound: Final value", BoundAttribute.Value, 150.0f);

		// Test unbound attribute
		UnboundAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 30.0f, 0, EGMCModifierPhase::EMP_Stack, true));
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

		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.PendingModifiers.Add(Mod3);

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

		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(15.0f);
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.ProcessPendingModifiers(16.0f);
		TestAttribute.PendingModifiers.Add(Mod3);
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

		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(15.0f);
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.ProcessPendingModifiers(16.0f);
		TestAttribute.PendingModifiers.Add(Mod3);
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

		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.PendingModifiers.Add(Mod3);

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
		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 30.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect;
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.ProcessPendingModifiers(15.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 10.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect;
		TestAttribute.PendingModifiers.Add(Mod3);
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
		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.ProcessPendingModifiers(20.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect3;
		TestAttribute.PendingModifiers.Add(Mod3);
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
		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 35.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.ProcessPendingModifiers(20.0f);

		FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod3.SourceAbilityEffect = Effect3;
		TestAttribute.PendingModifiers.Add(Mod3);
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

		TestAttribute.PendingModifiers.Add(Mod);
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

		TestAttribute.PendingModifiers.Add(Mod);
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
		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.ProcessPendingModifiers(10.0f);

		FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 200.0f, 0, EGMCModifierPhase::EMP_Stack, true);
		Mod2.SourceAbilityEffect = Effect2;
		TestAttribute.PendingModifiers.Add(Mod2);
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
	    BoundAttribute.PendingModifiers.Add(BoundMod);
	    BoundAttribute.ProcessPendingModifiers(10.0f);

	    // Add to unbound attribute (ConcatenatedHistory)
	    FGMCAttributeModifier UnboundMod = CreateModifier(EModifierType::Add, 25.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    UnboundMod.SourceAbilityEffect = Effect;
	    UnboundAttribute.PendingModifiers.Add(UnboundMod);
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
	    TestAttribute.PendingModifiers.Add(Mod1);
	    TestAttribute.ProcessPendingModifiers(10.0f);

	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::Add, 40.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod2.SourceAbilityEffect = Effect1;
	    TestAttribute.PendingModifiers.Add(Mod2);
	    TestAttribute.ProcessPendingModifiers(20.0f);

	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Add, 100.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod3.SourceAbilityEffect = Effect2;
	    TestAttribute.PendingModifiers.Add(Mod3);
	    TestAttribute.ProcessPendingModifiers(15.0f);

	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Add, 200.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    Mod4.SourceAbilityEffect = Effect3;
	    TestAttribute.PendingModifiers.Add(Mod4);
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

	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true));
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

	    TestAttribute.PendingModifiers.Add(Mod1);
	    TestAttribute.PendingModifiers.Add(Mod2);
	    TestAttribute.PendingModifiers.Add(Mod3);

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

		TestAttribute.PendingModifiers.Add(Mod1);
		TestAttribute.PendingModifiers.Add(Mod2);
		TestAttribute.PendingModifiers.Add(Mod3);

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
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::AddMultiplyBaseValue, 1.0f, 1, EGMCModifierPhase::EMP_Stack, true); // +100% = 2x
	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true);

	    // EMP_Post phase
	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true);
	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Multiply, 0.5f, 4, EGMCModifierPhase::EMP_Post, true); // +50% = 1.5x

	    Mod1.SourceAbilityEffect = Effect1;
	    Mod2.SourceAbilityEffect = Effect2;
	    Mod3.SourceAbilityEffect = Effect3;
	    Mod4.SourceAbilityEffect = Effect4;

	    TestAttribute.PendingModifiers.Add(Mod1); // 100 + (50*2) = 200
	    TestAttribute.PendingModifiers.Add(Mod2); // 200 -> 50 = -150
	    TestAttribute.PendingModifiers.Add(Mod3); // 50 -> 200 = 150
	    TestAttribute.PendingModifiers.Add(Mod4); // 200 * 1.5 = 300

	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 4 history entries
	    TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);

	    TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5

	    // Verify individual contributions
	    float Effect1Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false);
	    float Effect2Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
	    float Effect3Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false);
	    float Effect4Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect4, false);

	    TestEqual("AddMultiplyBaseValue delta", Effect1Delta, 100.0f); // 100 + (50*2) - 100 = 100
	    TestEqual("SetToBaseValue delta", Effect2Delta, -150.0f); // 200 -> 50 = -150
	    TestEqual("Set delta", Effect3Delta, 150.0f); // 50 -> 200 = 150
	    TestEqual("Multiply delta", Effect4Delta, 100.0f); // 200 * 1.5 - 200 = 100
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
	    FGMCAttributeModifier Mod1 = CreateModifier(EModifierType::AddMultiplyBaseValue, 1.0f, 1, EGMCModifierPhase::EMP_Stack, true); // +100% = 2x
	    FGMCAttributeModifier Mod2 = CreateModifier(EModifierType::SetToBaseValue, 0.0f, 2, EGMCModifierPhase::EMP_Stack, true);

	    // EMP_Post phase
	    FGMCAttributeModifier Mod3 = CreateModifier(EModifierType::Set, 200.0f, 3, EGMCModifierPhase::EMP_Post, true);
	    FGMCAttributeModifier Mod4 = CreateModifier(EModifierType::Multiply, 0.5f, 4, EGMCModifierPhase::EMP_Post, true); // +50% = 1.5x

	    Mod1.SourceAbilityEffect = Effect1;
	    Mod2.SourceAbilityEffect = Effect2;
	    Mod3.SourceAbilityEffect = Effect3;
	    Mod4.SourceAbilityEffect = Effect4;

	    TestAttribute.PendingModifiers.Add(Mod1); // 100 + (50*2) = 200
	    TestAttribute.PendingModifiers.Add(Mod2); // 200 -> 50 = -150
	    TestAttribute.PendingModifiers.Add(Mod3); // 50 -> 200 = 150
	    TestAttribute.PendingModifiers.Add(Mod4); // 200 * 1.5 = 300

	    TestAttribute.ProcessPendingModifiers(50.0f);

	    // Should have 4 history entries
	    TestEqual("Complex multi-phase: Entry count", TestAttribute.ModifierHistory.Num(), 4);

	    TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5

	    // Verify individual contributions
	    float Effect1Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect1, false);
	    float Effect2Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect2, false);
	    float Effect3Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect3, false);
	    float Effect4Delta = TestAttribute.ModifierHistory.ExtractFromMoveHistory(Effect4, false);

	    TestEqual("AddMultiplyBaseValue delta", Effect1Delta, 100.0f); // 100 + (50*2) - 100 = 100
	    TestEqual("SetToBaseValue delta", Effect2Delta, -150.0f); // 200 -> 50 = -150
	    TestEqual("Set delta", Effect3Delta, 150.0f); // 50 -> 200 = 150
	    TestEqual("Multiply delta", Effect4Delta, 100.0f); // 200 * 1.5 - 200 = 100
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
	        EModifierType OpType = static_cast<EModifierType>(i % 5);
	        int32 Priority = i % 10;
	        float Value = (i % 3 == 0) ? 0.1f : -0.05f;
	        EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
	        
	        TestAttribute.PendingModifiers.Add(CreateModifier(OpType, Value, Priority, Phase, true));
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
	    TestAttribute.PendingModifiers.Add(PastMod);
	    TestAttribute.ProcessPendingModifiers(10.0f);

	    FGMCAttributeModifier CurrentMod = CreateModifier(EModifierType::Add, 40.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    CurrentMod.SourceAbilityEffect = CurrentEffect;
	    TestAttribute.PendingModifiers.Add(CurrentMod);
	    TestAttribute.ProcessPendingModifiers(20.0f);

	    FGMCAttributeModifier FutureMod = CreateModifier(EModifierType::Add, 60.0f, 0, EGMCModifierPhase::EMP_Stack, true);
	    FutureMod.SourceAbilityEffect = FutureEffect;
	    TestAttribute.PendingModifiers.Add(FutureMod);
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
	    FGMCAttributeModifier Mod = CreateModifier(EModifierType::Multiply, -0.6f, 0, EGMCModifierPhase::EMP_Stack, true); // -60% = 0.4x
	    Mod.SourceAbilityEffect = Effect;

	    TestAttribute.PendingModifiers.Add(Mod);
	    TestAttribute.ProcessPendingModifiers(50.0f);

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
        TestAttribute.PendingModifiers.Add(DoTMod);

        // Buff Effect - multiplicative bonus every 10 frames
        if (Frame % 10 == 0)
        {
            FGMCAttributeModifier BuffMod = CreateModifier(EModifierType::Multiply, 0.01f, 1, EGMCModifierPhase::EMP_Stack, true); // +1%
            BuffMod.SourceAbilityEffect = BuffEffect;
            TestAttribute.PendingModifiers.Add(BuffMod);
        }

        // Debuff Effect - base value modifier every 30 frames
        if (Frame % 30 == 0)
        {
            FGMCAttributeModifier DebuffMod = CreateModifier(EModifierType::AddMultiplyBaseValue, -0.02f, 2, EGMCModifierPhase::EMP_Post, true); // -2%
            DebuffMod.SourceAbilityEffect = DebuffEffect;
            TestAttribute.PendingModifiers.Add(DebuffMod);
        }

        // Heal Effect - periodic healing every 60 frames (1 second)
        if (Frame % 60 == 0 && Frame > 0)
        {
            FGMCAttributeModifier HealMod = CreateModifier(EModifierType::Add, 25.0f, 3, EGMCModifierPhase::EMP_Post, true);
            HealMod.SourceAbilityEffect = HealEffect;
            TestAttribute.PendingModifiers.Add(HealMod);
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
            EModifierType RandomOp = static_cast<EModifierType>(FMath::RandRange(0, 4));
            
            // Random value based on operator
            float RandomValue = 0.0f;
            switch (RandomOp)
            {
                case EModifierType::Add:
                    RandomValue = FMath::FRandRange(-50.0f, 100.0f);
                    break;
                case EModifierType::Multiply:
                    RandomValue = FMath::FRandRange(-0.5f, 1.0f);
                    break;
                case EModifierType::AddMultiplyBaseValue:
                    RandomValue = FMath::FRandRange(-0.3f, 0.8f);
                    break;
                case EModifierType::Set:
                    RandomValue = FMath::FRandRange(100.0f, 3000.0f);
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
            TestAttribute.PendingModifiers.Add(Modifier);
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
            EModifierType RandomOp = static_cast<EModifierType>(FMath::RandRange(0, 4));
            
            float RandomValue = 0.0f;
            switch (RandomOp)
            {
                case EModifierType::Add:
                    RandomValue = FMath::FRandRange(-30.0f, 60.0f); // Smaller range for replay
                    break;
                case EModifierType::Multiply:
                    RandomValue = FMath::FRandRange(-0.3f, 0.7f);
                    break;
                case EModifierType::AddMultiplyBaseValue:
                    RandomValue = FMath::FRandRange(-0.2f, 0.5f);
                    break;
                case EModifierType::Set:
                    RandomValue = FMath::FRandRange(200.0f, 2500.0f);
                    break;
                case EModifierType::SetToBaseValue:
                    RandomValue = 0.0f;
                    break;
            }
            
            int32 RandomPriority = FMath::RandRange(0, 8);
            EGMCModifierPhase RandomPhase = (FMath::RandBool()) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
            
            FGMCAttributeModifier Modifier = CreateModifier(RandomOp, RandomValue, RandomPriority, RandomPhase, true);
            Modifier.SourceAbilityEffect = RandomEffect;
            TestAttribute.PendingModifiers.Add(Modifier);
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
	return true;
};



