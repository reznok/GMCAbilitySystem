// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Tasks/WaitForAbilityEffectStackChange.h"


UGMCAbilityTask_WaitForAbilityEffectStackChange* UGMCAbilityTask_WaitForAbilityEffectStackChange::WaitForAbilityEffectStackChange(UGMCAbility* OwningAbility, int32 InHandle)
{
	UGMCAbilityTask_WaitForAbilityEffectStackChange* MyObj = NewAbilityTask<UGMCAbilityTask_WaitForAbilityEffectStackChange>(OwningAbility);
	MyObj->Handle = InHandle;
	return MyObj;
}

void UGMCAbilityTask_WaitForAbilityEffectStackChange::Activate()
{
	Super::Activate();

	AbilitySystemComponent = Ability->OwnerAbilityComponent;
	int32 EffectNetworkId;
	UGMCAbilityEffect* AbilityEffect = nullptr;

	// Appel de la méthode GetEffectFromHandle


		bool bFound = AbilitySystemComponent->GetEffectFromHandle(Handle, EffectNetworkId, AbilityEffect);

		if (bFound)
		{
			if(AbilityEffect->StackingType !=  EAbilityEffectStackingType::None)
			{
				FOnActiveAbilityEffectStackChange* DelPtr = AbilitySystemComponent->OnAbilityEffectStackChangeDelegate(Handle);
				if (DelPtr)
				{
					
					UE_LOG(LogTemp, Log, TEXT("Binding delegate for Handle: %d"), Handle);
					OnAbilityEffectStackChangeDelegateHandle = DelPtr->AddUObject(this, &UGMCAbilityTask_WaitForAbilityEffectStackChange::OnAbilityEffectStackChange);
				}
			}
			else
			{
				// L'effet n'a pas été trouvé
				//UE_LOG(LogTemp, Warning, TEXT("Effect with handle %d not found!"), Handle);
				InvalidHandle.Broadcast(Handle, 0, 0);
				EndTask();
				return;;
			}
		}
		else
		{
			// L'effet n'a pas été trouvé
			//UE_LOG(LogTemp, Warning, TEXT("Effect with handle %d not found!"), Handle);
			InvalidHandle.Broadcast(Handle, 0, 0);
			EndTask();
			return;;
		}
	

	
	
	
}

void UGMCAbilityTask_WaitForAbilityEffectStackChange::OnDestroy(bool bInOwnerFinished)
{

	UGMC_AbilitySystemComponent* EffectOwningAbilitySystemComponent = Ability->OwnerAbilityComponent;
	if (EffectOwningAbilitySystemComponent && OnAbilityEffectStackChangeDelegateHandle.IsValid())
	{
		FOnActiveAbilityEffectStackChange* DelPtr = EffectOwningAbilitySystemComponent->OnAbilityEffectStackChangeDelegate(Handle);
		if (DelPtr)
		{
			DelPtr->Remove(OnAbilityEffectStackChangeDelegateHandle);
		}
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForAbilityEffectStackChange::OnAbilityEffectStackChange(int32 handle, int32 NewCount,
                                                                                 int32 OldCount)
{
	UE_LOG(LogTemp, Log, TEXT("Delegate called: Handle: %d, OldCount: %d, NewCount: %d"), handle, OldCount, NewCount);

	OnChange.Broadcast(handle, NewCount, OldCount);

}


