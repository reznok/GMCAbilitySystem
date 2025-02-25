// Copyright Epic Games, Inc. All Rights Reserved.

#include "GMCAbilitySystem.h"
#include "GMCAbilitySystemGlobals.h"

#define LOCTEXT_NAMESPACE "FGMCAbilitySystemModule"
DEFINE_LOG_CATEGORY(LogGMCAbilitySystem);

UGMCAbilitySystemGlobals* FGMCAbilitySystemModule::AbilitySystemGlobals = nullptr;

void FGMCAbilitySystemModule::StartupModule()
{
    // This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
#if WITH_GAMEPLAY_DEBUGGER
    IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
    GameplayDebuggerModule.RegisterCategory("GMCAbilitySystem", IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_GMCAbilitySystem::MakeInstance), EGameplayDebuggerCategoryState::EnabledInGameAndSimulate, 9);
    GameplayDebuggerModule.NotifyCategoriesChanged();
#endif
}

void FGMCAbilitySystemModule::ShutdownModule()
{
    // This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
    // we call this function before unloading the module.
}

UGMCAbilitySystemGlobals* FGMCAbilitySystemModule::GetAbilitySystemGlobals()
{
    if (!AbilitySystemGlobals)
    {
        QUICK_SCOPE_CYCLE_COUNTER(STAT_IGameplayAbilitiesModule_GetAbilitySystemGlobals_LoadModule);
        FSoftClassPath AbilitySystemClassName = (UGMCAbilitySystemGlobals::StaticClass()->GetDefaultObject<UGMCAbilitySystemGlobals>())->AbilitySystemGlobalsClassName;

        // Check if the class name is empty
        if (AbilitySystemClassName.ToString().IsEmpty())
        {
            UE_LOG(LogTemp, Error, TEXT("AbilitySystemGlobalsClassName is empty or invalid!"));
            return nullptr;  // Handle the error or fallback gracefully
        }

        UE_LOG(LogTemp, Warning, TEXT("AbilitySystemGlobalsClassName: %s"), *AbilitySystemClassName.ToString());

        UClass* SingletonClass = AbilitySystemClassName.TryLoadClass<UObject>();
        checkf(SingletonClass != nullptr, TEXT("Ability config value AbilitySystemGlobalsClassName is not a valid class name."));

        AbilitySystemGlobals = NewObject<UGMCAbilitySystemGlobals>(GetTransientPackage(), SingletonClass, NAME_None);
        AbilitySystemGlobals->AddToRoot();
        AbilitySystemGlobals->InitGlobalData();
        AbilitySystemGlobalsReadyCallback.Broadcast();
    }

    check(AbilitySystemGlobals);
    return AbilitySystemGlobals;
}


#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGMCAbilitySystemModule, GMCAbilitySystem)