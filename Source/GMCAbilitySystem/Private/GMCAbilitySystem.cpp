// Copyright Epic Games, Inc. All Rights Reserved.

#include "GMCAbilitySystem.h"

#define LOCTEXT_NAMESPACE "FGMCAbilitySystemModule"
DEFINE_LOG_CATEGORY(LogGMCAbilitySystem);

void FGMCAbilitySystemModule::StartupModule()
{
	// Version banner — confirms which submodule commit was actually compiled into the running binary.
	// Bump the tag string whenever the submodule is bumped so live builds can be checked via log grep
	// for "[GMAS-LIVE-CHECK]". The __DATE__/__TIME__ macros catch stale binaries that weren't rebuilt.
	UE_LOG(LogGMCAbilitySystem, Warning,
		TEXT("[GMAS-LIVE-CHECK] Module loaded — build tag: %s | compiled: %s %s"),
		TEXT("2026-05-04-aherys-post-dbg-cleanup"),
		TEXT(__DATE__), TEXT(__TIME__));

	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	/*...*/
	#if WITH_GAMEPLAY_DEBUGGER
	IGameplayDebugger& GameplayDebuggerModule = IGameplayDebugger::Get();
	GameplayDebuggerModule.RegisterCategory("GMCAbilitySystem", IGameplayDebugger::FOnGetCategory::CreateStatic(&FGameplayDebuggerCategory_GMCAbilitySystem::MakeInstance), EGameplayDebuggerCategoryState::EnabledInGameAndSimulate, 9);
	GameplayDebuggerModule.NotifyCategoriesChanged();
	#endif
	/*...*/
}

void FGMCAbilitySystemModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGMCAbilitySystemModule, GMCAbilitySystem)