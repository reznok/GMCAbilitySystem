// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_GAMEPLAY_DEBUGGER
	#include "GameplayDebugger.h"
	#include "Debug/GameplayDebuggerCategory_GMCAbilitySystem.h"
#endif // WITH_GAMEPLAY_DEBUGGER

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGMCAbilitySystem, Log, All);


 class GMCABILITYSYSTEM_API FGMCAbilitySystemModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};

//////////////////////////////////////////////////////////////////////////
// Enum To String
// Usage Example:
//		FString EnumString = EnumToString( EnumValue );
// Sourced from: https://forums.unrealengine.com/t/conversion-of-enum-to-string/337869/24?u=petergilbz
//////////////////////////////////////////////////////////////////////////
template< typename T >
FString EnumToString( T EnumValue )
{
	static_assert( TIsUEnumClass< T >::Value, "'T' template parameter to EnumToString must be a valid UEnum" );
	return StaticEnum< T >()->GetNameStringByValue( ( int64 ) EnumValue );
}