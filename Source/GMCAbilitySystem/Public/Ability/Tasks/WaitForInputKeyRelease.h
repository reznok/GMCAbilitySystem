#pragma once
#include "EnhancedInputComponent.h"
#include "GMCAbilityTaskBase.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "WaitForInputKeyRelease.generated.h"

// DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUWaitForInputKeyReleaseAsyncActionPin, float, DurationHeld);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskWaitForInputKeyRelease);


UCLASS()
class UGMCAbilityTask_WaitForInputKeyRelease : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:

	UFUNCTION()
	virtual void Tick(float DeltaTime) override;

	void OnTaskCompleted();
	virtual void OnDestroy(bool bInOwnerFinished) override;

	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;
	 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait For Input Key Release",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_WaitForInputKeyRelease* WaitForKeyRelease(UGMCAbility* OwningAbility);
 
	//Overriding BP async action base
	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitForInputKeyRelease Completed;

private:
	FEnhancedInputActionValueBinding* InputActionBinding;
	UEnhancedInputComponent* GetEnhancedInputComponent() const;

	// Todo: Add duration back in
	float StartTime;
	float Duration;
	double OldTime;
	
};