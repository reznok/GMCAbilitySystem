#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "EnhancedInputComponent.h"
#include "Ability/GMCAbility.h"
#include "InstancedStruct.h"
#include "LatentActions.h"
#include "WaitForInputKeyPressParameterized.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAbilityTaskWaitForInputKeyPressParameterized);

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForInputKeyPressParameterized : public UGMCAbilityTaskBase {
	GENERATED_BODY()

public:
	
	void OnTaskCompleted();
	virtual void OnDestroy(bool bInOwnerFinished) override;

	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
	 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility"), DisplayName = "Wait For Input Key Press Parameterized", Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_WaitForInputKeyPressParameterized* WaitForKeyPress(UGMCAbility* OwningAbility, UInputAction* InputAction);

	//Overriding BP async action base
	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPressParameterized Completed;

	UPROPERTY()
	UInputAction* InputActionToWaitFor = nullptr;

protected:
	
	void OnKeyPressed(const FInputActionValue& InputActionValue);

private:
	
	UEnhancedInputComponent* GetEnhancedInputComponent() const;

	int64 InputBindingHandle = -1;
};
