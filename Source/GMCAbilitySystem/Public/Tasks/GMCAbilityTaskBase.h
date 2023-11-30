#pragma once
#include "Engine/CancellableAsyncAction.h"
#include "GMCAbilityTaskBase.generated.h"

class UGMCAbility;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FGMCAbilityTaskOutputPin);

UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTaskBase : public UCancellableAsyncAction
{
	GENERATED_BODY()
	
public:
	UPROPERTY()
	UGMCAbility* OwningAbility;

	UPROPERTY()
	int TaskID;

	FDelegateHandle TickHandle;

	//Overriding BP async action base
	virtual void Activate() override;
	
	virtual void InternalTick(float DeltaTime);
	virtual void InternalCompleted(bool Forced);
	virtual void InternalClientCompleted();

	UPROPERTY(BlueprintAssignable)
	FGMCAbilityTaskOutputPin Completed;

	void CompleteTask(bool Forced) {InternalCompleted(Forced);};

protected:
	bool bTaskCompleted;
};
