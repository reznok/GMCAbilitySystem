#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "EnhancedInputComponent.h"
#include "Ability/GMCAbility.h"
#include "InstancedStruct.h"
#include "LatentActions.h"
#include "WaitForInputKeyPress.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FAbilityTaskWaitForInputKeyPress);

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
	 
	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait For Input Key Press",Category = "GMCAbilitySystem/Tasks")
	static UGMCAbilityTask_WaitForInputKeyPress* WaitForKeyPress(UGMCAbility* OwningAbility);

	//Overriding BP async action base
	virtual void Activate() override;

	UPROPERTY(BlueprintAssignable)
	FAbilityTaskWaitForInputKeyPress Completed;

protected:
	
	void OnKeyPressed(const FInputActionValue& InputActionValue);

private:
	
	UEnhancedInputComponent* GetEnhancedInputComponent() const;

	int64 InputBindingHandle = -1;
};
