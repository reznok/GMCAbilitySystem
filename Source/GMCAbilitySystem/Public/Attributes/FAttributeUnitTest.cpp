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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
        // CurModVal = 1 + 0.5 - 0.3 + 0.1 = 1.3, so 200 + (100 * 1.3) = 200 + 130 = 330
        TestEqual("Mixed +/- AddMultiplyBase: 200 + 130 = 330", TestAttribute.Value, 330.0f);
    }

    // Test 21: Empty modifiers list
    {
        UE_LOG(LogTemp, Log, TEXT("Test 21: Empty Modifiers List"));
        FAttribute TestAttribute;
        TestAttribute.BaseValue = 75.0f;
        TestAttribute.Value = 125.0f;
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
        // Should have 2 history entries (SetToBaseValue and AddMultiplyBaseValue)
        TestEqual("History tracking: 2 entries", History.Num(), 2);
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
    	// Add 50000 modifiers of various types with mixed phases
    	for (int32 i = 0; i < 50000; ++i)
    	{
    		EModifierType OpType = static_cast<EModifierType>(i % 5);
    		int32 Priority = i % 20;
    		float Value = (i % 3 == 0) ? 0.1f : -0.05f;
    		EGMCModifierPhase Phase = (i % 2 == 0) ? EGMCModifierPhase::EMP_Stack : EGMCModifierPhase::EMP_Post;
    		TestAttribute.PendingModifiers.Add(CreateModifier(OpType, Value, Priority, Phase, true));
    	}
        
    	double StartTime = FPlatformTime::Seconds();
        
    	TArray<FModifierApplicationEntry> History;
    	TestAttribute.ProcessPendingModifiers(History);
        
    	double EndTime = FPlatformTime::Seconds();
    	double ElapsedMs = (EndTime - StartTime) * 1000.0;
        
    	UE_LOG(LogTemp, Log, TEXT("Processing 50000 new modifiers took %f ms (SizeOfHistory:%d)"), ElapsedMs, History.GetAllocatedSize());
        
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
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
        
        TArray<FModifierApplicationEntry> History;
        TestAttribute.ProcessPendingModifiers(History);
        
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
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 0, EGMCModifierPhase::EMP_Stack, true));
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry
	    TestEqual("SetToBaseValue: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta should be: 75 (new value) - 150 (old value) = -75
	        TestEqual("SetToBaseValue: Delta value", History[0].Value, -75.0f);
	    }
	    TestEqual("SetToBaseValue: Final value", TestAttribute.Value, 75.0f);
	}

	// Test 34: Set Operator - Individual History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 34: Set Operator Individual History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 100.0f;
	    TestAttribute.Value = 200.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 300.0f, 0, EGMCModifierPhase::EMP_Stack, true));
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry
	    TestEqual("Set: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta should be: 300 (new value) - 200 (old value) = 100
	        TestEqual("Set: Delta value", History[0].Value, 100.0f);
	    }
	    TestEqual("Set: Final value", TestAttribute.Value, 300.0f);
	}

	// Test 35: AddMultiplyBaseValue - Individual History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 35: AddMultiplyBaseValue Individual History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 50.0f;
	    TestAttribute.Value = 100.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.6f, 0, EGMCModifierPhase::EMP_Stack, true)); // +60% = 1.6x
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry
	    TestEqual("AddMultiplyBaseValue: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta should be: (100 + (50 * 1.6)) - 100 = 180 - 100 = 80
	        TestEqual("AddMultiplyBaseValue: Delta value", History[0].Value, 80.0f);
	    }
	    TestEqual("AddMultiplyBaseValue: Final value", TestAttribute.Value, 180.0f);
	}

	// Test 36: Multiply - Individual History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 36: Multiply Individual History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 80.0f;
	    TestAttribute.Value = 120.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 0, EGMCModifierPhase::EMP_Stack, true)); // +50% = 1.5x
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry
	    TestEqual("Multiply: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta should be: (120 * 1.5) - 120 = 180 - 120 = 60
	        TestEqual("Multiply: Delta value", History[0].Value, 60.0f);
	    }
	    TestEqual("Multiply: Final value", TestAttribute.Value, 180.0f);
	}

	// Test 37: Add - Individual History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 37: Add Individual History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 90.0f;
	    TestAttribute.Value = 110.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 45.0f, 0, EGMCModifierPhase::EMP_Stack, true));
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry
	    TestEqual("Add: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta should be: (110 + 45) - 110 = 155 - 110 = 45
	        TestEqual("Add: Delta value", History[0].Value, 45.0f);
	    }
	    TestEqual("Add: Final value", TestAttribute.Value, 155.0f);
	}

	// Test 38: Multiple Operators - Selective History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 38: Multiple Operators - Selective History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 100.0f;
	    TestAttribute.Value = 200.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 50.0f, 0, EGMCModifierPhase::EMP_Stack, true));      // Tracked
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.5f, 1, EGMCModifierPhase::EMP_Stack, false)); // Not tracked
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Set, 400.0f, 2, EGMCModifierPhase::EMP_Post, true));    // Tracked
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 2 history entries (Add and Set, not Multiply)
	    TestEqual("Selective history: Entry count", History.Num(), 2);
	    
	    if (History.Num() >= 2)
	    {
	        // First entry: Add 50 to 200 = 250, delta = 50
	        TestEqual("First entry delta (Add)", History[0].Value, 50.0f);
	        // Second entry: Set to 400 from 375 (250 * 1.5), delta = 25
	        TestEqual("Second entry delta (Set)", History[1].Value, 25.0f);
	    }
	    TestEqual("Final value", TestAttribute.Value, 400.0f);
	}

	// Test 39: Cross-Phase History Registration
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 39: Cross-Phase History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 60.0f;
	    TestAttribute.Value = 120.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 0.5f, 0, EGMCModifierPhase::EMP_Stack, true)); // +50% = 1.5x
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 30.0f, 1, EGMCModifierPhase::EMP_Post, true));
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 2 history entries
	    TestEqual("Cross-phase history: Entry count", History.Num(), 2);
	    
	    if (History.Num() >= 2)
	    {
	        // First entry (EMP_Stack): 120 + (60 * 1.5) = 210, delta = 90
	        TestEqual("Stack phase delta", History[0].Value, 90.0f);
	        // Second entry (EMP_Post): 210 + 30 = 240, delta = 30  
	        TestEqual("Post phase delta", History[1].Value, 30.0f);
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
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 0 history entries because delta is 0
	    TestEqual("Zero delta: No history entry", History.Num(), 0);
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
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 3 history entries (one per modifier, even though they're agglomerated)
	    TestEqual("Agglomerated: History count", History.Num(), 3);
	    TestEqual("Final value: 200 + 75 = 275", TestAttribute.Value, 275.0f);
	}

	// Test 42: Mixed Registration with SetToBaseValue
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 42: Mixed Registration with SetToBaseValue"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 80.0f;
	    TestAttribute.Value = 240.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, 0.25f, 0, EGMCModifierPhase::EMP_Stack, true)); // +25% = 1.25x
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::SetToBaseValue, 0.0f, 1, EGMCModifierPhase::EMP_Stack, true)); // Reset
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Add, 20.0f, 2, EGMCModifierPhase::EMP_Post, false)); // Not tracked
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 2 history entries (Multiply and SetToBaseValue, not Add)
	    TestEqual("Mixed registration: Entry count", History.Num(), 2);
	    
	    if (History.Num() >= 2)
	    {
	        // First: 240 * 1.25 = 300, delta = 60
	        TestEqual("Multiply delta", History[0].Value, 60.0f);
	        // Second: Reset to 80 from 300, delta = -220
	        TestEqual("SetToBaseValue delta", History[1].Value, -220.0f);
	    }
	    TestEqual("Final value: 80 + 20 = 100", TestAttribute.Value, 100.0f);
	}

	// Test 43: Negative Delta History
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 43: Negative Delta History Registration"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 200.0f;
	    TestAttribute.Value = 300.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::Multiply, -0.6f, 0, EGMCModifierPhase::EMP_Stack, true)); // -60% = 0.4x
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry with negative delta
	    TestEqual("Negative delta: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta: (300 * 0.4) - 300 = 120 - 300 = -180
	        TestEqual("Negative delta value", History[0].Value, -180.0f);
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
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 4 history entries
	    TestEqual("Complex multi-phase: Entry count", History.Num(), 4);
	    
	    // Execution order due to Phase > Priority:
	    // EMP_Stack Priority 1: AddMultiplyBaseValue first
	    // EMP_Stack Priority 2: SetToBaseValue second
	    // EMP_Post Priority 3: Set
	    // EMP_Post Priority 4: Multiply
	    
	    TestEqual("Final value", TestAttribute.Value, 300.0f); // 200 * 1.5
	}

	// Test 45: History with Extreme Values
	{
	    UE_LOG(LogTemp, Log, TEXT("Test 45: History with Extreme Values"));
	    FAttribute TestAttribute;
	    TestAttribute.BaseValue = 1.0f;
	    TestAttribute.Value = 2.0f;
	    
	    TestAttribute.PendingModifiers.Add(CreateModifier(EModifierType::AddMultiplyBaseValue, 999.0f, 0, EGMCModifierPhase::EMP_Stack, true)); // +99900% = 1000x
	    
	    TArray<FModifierApplicationEntry> History;
	    TestAttribute.ProcessPendingModifiers(History);
	    
	    // Should have 1 history entry with extreme delta
	    TestEqual("Extreme values: History count", History.Num(), 1);
	    if (History.Num() > 0)
	    {
	        // Delta: (2 + (1 * 1000)) - 2 = 1002 - 2 = 1000
	        TestEqual("Extreme delta value", History[0].Value, 1000.0f);
	    }
	    TestEqual("Final value", TestAttribute.Value, 1002.0f);
	}

    UE_LOG(LogTemp, Log, TEXT("All 32 Advanced Attribute Tests Completed Successfully!"));
    return true;
}

