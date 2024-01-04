// #pragma once
// #include "GMCAbility.h"
// #include "LatentActions.h"
// #include "Engine/CancellableAsyncAction.h"
// #include "WaitForInputKeyRelease.generated.h"
//
// DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUWaitForInputKeyReleaseAsyncActionPin, float, DurationHeld);
//
// UCLASS()
// class UWaitForInputKeyReleaseAsyncAction : public UGMCAbilityTaskBase
// {
// 	GENERATED_BODY()
// 	
// public:
// 	UFUNCTION()
// 	virtual void TickTask(float DeltaTime) override;
//  
// 	UFUNCTION()
// 	virtual void InternalCompleted(bool Forced) override;
//
// 	// Start UObject Functions
// 	virtual UWorld* GetWorld() const override
// 	{
// 		return ContextWorld.IsValid() ? ContextWorld.Get() : nullptr;
// 	}
// 	// End UObject Functions
//  
// 	UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContext", HidePin = "Ability", DefaultToSelf = "Ability", BlueprintInternalUseOnly = "TRUE"), DisplayName="Wait For Input Key Release",Category = "GMCAbilitySystem/Tasks")
// 	static UWaitForInputKeyReleaseAsyncAction* WaitForKeyRelease(const UObject* WorldContext, UGMCAbility* Ability);
//  
// 	//Overriding BP async action base
// 	virtual void Activate() override;
// 	
//
// private:
// 	/** The context world of this action. */
// 	TWeakObjectPtr<UWorld> ContextWorld = nullptr;
//
// 	int InputReleaseBindHandle = -1;
//
// 	UEnhancedInputComponent* GetEnhancedInputComponent() const;
// 	
// 	float StartTime;
// 	float Duration;
// };