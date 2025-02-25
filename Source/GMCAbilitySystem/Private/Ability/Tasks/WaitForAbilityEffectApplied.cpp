// Fill out your copyright notice in the Description page of Project Settings.


#include "Ability/Tasks/WaitForAbilityEffectApplied.h"



UGMCAbilityTask_WaitForAbilityEffectApplied::UGMCAbilityTask_WaitForAbilityEffectApplied(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), TriggerOnce(false), ListenForPeriodicEffects(false), Handle(0), Registered(false)
{
}


void UGMCAbilityTask_WaitForAbilityEffectApplied::SetExternalActor(AActor* InActor)
{
	if (InActor)
	{
		UseExternalOwner = true;
		ExternalOwner = Cast<UGMC_AbilitySystemComponent>(InActor->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
	}
}

UGMCAbilityTask_WaitForAbilityEffectApplied* UGMCAbilityTask_WaitForAbilityEffectApplied::WaitForAbilityEffectApplied(UGMCAbility* OwningAbility, FGameplayTagRequirements InTagRequirements, bool InTriggerOnce, bool InListenForPeriodicEffect,AActor* OptionalExternalOwner)
{
	UGMCAbilityTask_WaitForAbilityEffectApplied* MyObj = NewAbilityTask<UGMCAbilityTask_WaitForAbilityEffectApplied>(OwningAbility);
	MyObj->TagRequirements = InTagRequirements;
	MyObj->TriggerOnce = InTriggerOnce;
	MyObj->SetExternalActor(OptionalExternalOwner);
	MyObj->ListenForPeriodicEffects = InListenForPeriodicEffect;
	return MyObj;
}

void UGMCAbilityTask_WaitForAbilityEffectApplied::OnApplyAbilityEffectCallback(AActor* Target,
	int32 ActiveHandle)
{
	AActor* AvatarActor = nullptr;

	UGMC_AbilitySystemComponent* AbilityComp = nullptr;
			
	if(UseExternalOwner)
	{
		AvatarActor = ExternalOwner->GetOwner();
		AbilityComp = Cast<UGMC_AbilitySystemComponent>(AvatarActor->GetComponentByClass(UGMC_AbilitySystemComponent::StaticClass()));
			
	}
	else{
		AvatarActor =  Ability->OwnerAbilityComponent->GetOwner();
		AbilityComp = Ability->OwnerAbilityComponent;
	}


	if (!TagRequirements.RequirementsMet(AbilityComp->GetActiveEffectTags()))
	{
		return;
	}
	OnApplied.Broadcast(AvatarActor,ActiveHandle);


	if (TriggerOnce)
	{
		EndTask();
			
}
}

void UGMCAbilityTask_WaitForAbilityEffectApplied::Activate()
{
	Super::Activate();

	AbilitySystemComponent = Ability->OwnerAbilityComponent;

	
	RegisterDelegate();
}


void UGMCAbilityTask_WaitForAbilityEffectApplied::OnDestroy(bool bInOwnerFinished)
{

	UGMC_AbilitySystemComponent* EffectOwningAbilitySystemComponent = Ability->OwnerAbilityComponent;
	if (EffectOwningAbilitySystemComponent && OnAbilityEffectAppliedDelegateHandle.IsValid())
	{
		EffectOwningAbilitySystemComponent->OnAbilityEffectAppliedDelegate.Remove(OnApplyAbilityEffectCallbackDelegateHandle);
		if(OnApplyAbilityEffectCallbackDelegateHandle.IsValid())
		{
			EffectOwningAbilitySystemComponent->OnPeriodicAbilityEffectExecuteDelegate.Remove(OnPeriodicAbilityEffectExecuteCallbackDelegateHandle);
		}
	}

	Super::OnDestroy(bInOwnerFinished);
}

void UGMCAbilityTask_WaitForAbilityEffectApplied::RegisterDelegate()
{

		OnApplyAbilityEffectCallbackDelegateHandle = AbilitySystemComponent->OnAbilityEffectAppliedDelegate.AddUObject(this, &UGMCAbilityTask_WaitForAbilityEffectApplied::OnApplyAbilityEffectCallback);
	
	
	if (ListenForPeriodicEffects)
	{
		OnPeriodicAbilityEffectExecuteCallbackDelegateHandle =  AbilitySystemComponent->OnPeriodicAbilityEffectExecuteDelegate.AddUObject(this, &UGMCAbilityTask_WaitForAbilityEffectApplied::OnApplyAbilityEffectCallback);
	}
}

void UGMCAbilityTask_WaitForAbilityEffectApplied::OnAbilityEffectApplied(AActor* Source, int EffectHandle)

{
	

	OnApplied.Broadcast(Source,EffectHandle);
	
}


