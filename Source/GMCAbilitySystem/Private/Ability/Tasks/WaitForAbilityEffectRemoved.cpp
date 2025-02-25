// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Tasks/WaitForAbilityEffectRemoved.h"


UGMCAbilityTask_WaitForAbilityEffectRemoved* UGMCAbilityTask_WaitForAbilityEffectRemoved::WaitForAbilityEffectRemoved(UGMCAbility* OwningAbility, int32 InHandle)
{
	UGMCAbilityTask_WaitForAbilityEffectRemoved* MyObj = NewAbilityTask<UGMCAbilityTask_WaitForAbilityEffectRemoved>(OwningAbility);
	MyObj->Handle = InHandle;
	return MyObj;
}

void UGMCAbilityTask_WaitForAbilityEffectRemoved::Activate()
{
	Super::Activate();

	AbilitySystemComponent = Ability->OwnerAbilityComponent;
	int32 EffectNetworkId;
	UGMCAbilityEffect* AbilityEffect = nullptr;


	FAbilityEffectRemovalInfo EmptyAbilityEffectRemovalInfo;


	// Appel de la méthode GetEffectFromHandle


		bool bFound = AbilitySystemComponent->GetEffectFromHandle(Handle, EffectNetworkId, AbilityEffect);

		if (bFound){
	
				FOnActiveAbilityEffectRemoved* DelPtr = AbilitySystemComponent->OnAbilityEffectRemovedDelegate(Handle);
				if (DelPtr)
				{
					
					UE_LOG(LogTemp, Log, TEXT("Binding delegate for Handle: %d"), Handle);
					OnAbilityEffectRemoveDelegateHandle = DelPtr->AddUObject(this, &UGMCAbilityTask_WaitForAbilityEffectRemoved::OnAbilityEffectRemoved);
				}
			}

		else
		{
			InvalidHandle.Broadcast(EmptyAbilityEffectRemovalInfo);
			EndTask();
			return;;
		}
	

	
	
	
}

void UGMCAbilityTask_WaitForAbilityEffectRemoved::OnDestroy(bool bInOwnerFinished)
{

	UGMC_AbilitySystemComponent* EffectOwningAbilitySystemComponent = Ability->OwnerAbilityComponent;
	if (EffectOwningAbilitySystemComponent && OnAbilityEffectRemoveDelegateHandle.IsValid())
	{
		FOnActiveAbilityEffectRemoved* DelPtr = EffectOwningAbilitySystemComponent->OnAbilityEffectRemovedDelegate(Handle);
		if (DelPtr)
		{
			DelPtr->Remove(OnAbilityEffectRemoveDelegateHandle);
		}
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForAbilityEffectRemoved::OnAbilityEffectRemoved(const FAbilityEffectRemovalInfo& InAbilityEffectRemovalInfo
)
{
	

	OnRemoved.Broadcast(InAbilityEffectRemovalInfo);

}


