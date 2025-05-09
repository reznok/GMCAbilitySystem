#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "EnhancedInputComponent.h"
#include "Ability/GMCAbility.h"
#include "InstancedStruct.h"
#include "LatentActions.h"
#include "WaitForInputKeyPressParameterized.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityTaskWaitForInputKeyPressParameterized, float, Duration);

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
	 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility"), DisplayName = "Wait For Input Key Press Parameterized", Category = "GMAS|Abilities|Tasks")
	static UGMCAbilityTask_WaitForInputKeyPressParameterized* WaitForKeyPress(UGMCAbility* OwningAbility, UInputAction* InputAction, bool bCheckForPressDuringActivation = true, float MaxDuration = 0.0f);

	//Overriding BP async action base
	virtual void Activate() override;
	
	virtual void AncillaryTick(float DeltaTime) override;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPressParameterized Completed;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPressParameterized OnTick;

	// Called when duration goes over MaxDuration
	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPressParameterized TimedOut;

	UPROPERTY()
	UInputAction* InputActionToWaitFor = nullptr;

protected:
	
	void OnKeyPressed(const FInputActionValue& InputActionValue);
	
	/** If true, we may complete this task during activation if the ability's input action key is already released. */
	UPROPERTY(Transient)
	bool bShouldCheckForPressDuringActivation = false;

private:
	
	UEnhancedInputComponent* GetEnhancedInputComponent();

	int64 InputBindingHandle = -1;

	float MaxDuration;
	float StartTime;
	float Duration;
	bool bTimedOut;

	UPROPERTY()
	UInputComponent* InputComponent;
};
