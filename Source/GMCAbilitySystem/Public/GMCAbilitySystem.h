// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER
	#include "GameplayDebugger.h"
	#include "Debug\GameplayDebuggerCategory_GMCAbilitySystem.h"
#endif // WITH_GAMEPLAY_DEBUGGER

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGMCAbilitySystem, Log, All);


GMCABILITYSYSTEM_API class FGMCAbilitySystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
