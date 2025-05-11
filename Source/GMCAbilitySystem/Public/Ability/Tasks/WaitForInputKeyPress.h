#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "EnhancedInputComponent.h"
#include "Ability/GMCAbility.h"
#include "InstancedStruct.h"
#include "LatentActions.h"
#include "WaitForInputKeyPress.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAbilityTaskWaitForInputKeyPress, float, Duration);

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForInputKeyPress : public UGMCAbilityTaskBase {
	GENERATED_BODY()

public:
	
	void OnTaskCompleted();
	virtual void OnDestroy(bool bInOwnerFinished) override;

	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
	 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait For Input Key Press", Category = "GMAS|Abilities|Tasks")
	static UGMCAbilityTask_WaitForInputKeyPress* WaitForKeyPress(UGMCAbility* OwningAbility, bool bCheckForPressDuringActivation = true, float MaxDuration = 0.0f);

	//Overriding BP async action base
	virtual void Activate() override;
	
	virtual void AncillaryTick(float DeltaTime) override;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPress Completed;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPress OnTick;

	// Called when duration goes over MaxDuration
	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPress TimedOut;

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
