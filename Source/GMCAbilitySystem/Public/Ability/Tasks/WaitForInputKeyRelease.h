#pragma once
#include "EnhancedInputComponent.h"
#include "GMCAbilityTaskBase.h"
#include "Ability/GMCAbility.h"
#include "LatentActions.h"
#include "WaitForInputKeyRelease.generated.h"

// DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUWaitForInputKeyReleaseAsyncActionPin, float, DurationHeld);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskWaitForInputKeyRelease);


UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_WaitForInputKeyRelease : public UGMCAbilityTaskBase
{
	GENERATED_BODY()
	
public:
	
	void OnTaskCompleted();
	virtual void OnDestroy(bool bInOwnerFinished) override;

	virtual void ProgressTask(FInstancedStruct& TaskData) override;
	virtual void ClientProgressTask() override;

	/**
	 * Creates a new ability task that will wait for the owning ability's input action key to be released.
	 * @param OwningAbility The ability that owns this task.
	 * @param bCheckForReleaseDuringActivation If true, we may complete this task during activation if the ability's input action key is already released.
	 * @return 
	 */
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait For Input Key Release",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_WaitForInputKeyRelease* WaitForKeyRelease(UGMCAbility* OwningAbility, bool bCheckForReleaseDuringActivation = true);

	//Overriding BP async action base
	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskWaitForInputKeyRelease Completed;

protected:

	void OnKeyReleased(const FInputActionValue& InputActionValue);

	/** If true, we may complete this task during activation if the ability's input action key is already released. */
	UPROPERTY(Transient)
	bool bShouldCheckForReleaseDuringActivation = true;

private:
	
	UEnhancedInputComponent* GetEnhancedInputComponent() const;

	int64 InputBindingHandle = -1;
	
	// Todo: Add duration back in
	float StartTime;
	float Duration;
	double OldTime;
	
};