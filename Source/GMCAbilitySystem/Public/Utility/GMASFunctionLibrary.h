

#pragma once

#include "CoreMinimal.h"
#include "InputActionValue.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GMASFunctionLibrary.generated.h"

class UEnhancedPlayerInput;
class UInputAction;
struct FInputActionInstance;
/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMASFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get the InputActionInstance associated with the given input action. */
	UFUNCTION(BlueprintCallable, Category="Input")
	static FInputActionInstance GetInputActionInstance(APlayerController* InPlayerController, const UInputAction* ForAction);

	/** Get the value associated with the given input action. Useful for retrieving the value of an input inside an ability. */
	UFUNCTION(BlueprintCallable, Category="Input")
	static FInputActionValue GetInputActionValue(APlayerController* InPlayerController, const UInputAction* ForAction);
	
};
