
#pragma once

#include "CoreMinimal.h"
#include "Ability/Tasks/GMCAbilityTaskBase.h"
#include "SetTargetDataObject.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUGMCAbilityTaskTargetDataObjectAsyncActionPin, UObject*, Target);

USTRUCT(BlueprintType)
struct FGMCAbilityTaskTargetDataObject : public FGMCAbilityTaskData
{
	GENERATED_BODY()
	
	UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
	UObject* Target{nullptr};
};

/**
 * 
 */
UCLASS()
class GMCABILITYSYSTEM_API UGMCAbilityTask_SetTargetDataObject : public UGMCAbilityTaskBase {
	GENERATED_BODY()

	public:
		UPROPERTY(BlueprintAssignable)
		FUGMCAbilityTaskTargetDataObjectAsyncActionPin	Completed;
		
		UPROPERTY(BlueprintReadWrite, Category = "GMCAbilitySystem")
		UObject* Target{nullptr};
		
		virtual void ProgressTask(FInstancedStruct& TaskData) override;
		virtual void ClientProgressTask() override;
	 
		UFUNCTION(BlueprintCallable, meta = (BlueprintInternalUseOnly = "true", HidePin = "OwningAbility", DefaultToSelf = "OwningAbility", BlueprintInternalUseOnly = "TRUE"), DisplayName="Set Target Data (Object)",Category = "GMCAbilitySystem/Tasks")
		static UGMCAbilityTask_SetTargetDataObject* SetTargetDataObject(UGMCAbility* OwningAbility, UObject* Object);
	 
		//Overriding BP async action base
		virtual void Activate() override;
	
};
