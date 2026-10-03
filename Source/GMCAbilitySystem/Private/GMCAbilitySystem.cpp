// GMAS - GMC Ability System. MIT License, see LICENSE.

#include "GMCAbilitySystem.h"

#include "Interfaces/IPluginManager.h"

#define LOCTEXT_NAMESPACE "FGMCAbilitySystemModule"
DEFINE_LOG_CATEGORY(LogGMCAbilitySystem);

void FGMCAbilitySystemModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GMCAbilitySystem"));
	UE_LOG(LogGMCAbilitySystem, Log, TEXT("GMAS %s loaded"), Plugin.IsValid() ? *Plugin->GetDescriptor().VersionName : TEXT("(unknown version)"));

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

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FGMCAbilitySystemModule, GMCAbilitySystem)