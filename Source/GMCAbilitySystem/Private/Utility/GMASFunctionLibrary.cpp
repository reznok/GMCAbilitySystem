


#include "Utility/GMASFunctionLibrary.h"
#include "EnhancedPlayerInput.h"

FInputActionInstance UGMASFunctionLibrary::GetInputActionInstance(APlayerController* InPlayerController, const UInputAction* ForAction){
	if(!InPlayerController || !ForAction) return FInputActionInstance();

	if(const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(InPlayerController->PlayerInput)){
		return *EnhancedInput->FindActionInstanceData(ForAction);
	}
	
	return FInputActionInstance();
}

FInputActionValue UGMASFunctionLibrary::GetInputActionValue(APlayerController* InPlayerController, const UInputAction* ForAction){
	if(!InPlayerController || !ForAction) return FInputActionValue();

	if(const UEnhancedPlayerInput* EnhancedInput = Cast<UEnhancedPlayerInput>(InPlayerController->PlayerInput)){
		return EnhancedInput->GetActionValue(ForAction);
	}
	
	return FInputActionValue();
}
