#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GMASUtilities.generated.h"

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMASUtilities : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	// Editor-specific helper functions. Not actually for Blueprints.
#if WITH_EDITOR
	static void SetPropertyFlagsSafe(UClass* StaticClass, FName PropertyName, EPropertyFlags NewFlags);
	static void ClearPropertyFlagsSafe(UClass* StaticClass, FName PropertyName, EPropertyFlags ClearFlags);
#endif
	
};
