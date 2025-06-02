// Fill out your copyright notice in the Description page of Project Settings.


#include "Debug/GameplayDebuggerCategory_GMCAbilitySystem.h"

#include "Components/GMCAbilityComponent.h"

#if WITH_GAMEPLAY_DEBUGGER

FGameplayDebuggerCategory_GMCAbilitySystem::FGameplayDebuggerCategory_GMCAbilitySystem()
{
	SetDataPackReplication<FRepData>(&DataPack);
	bShowOnlyWithDebugActor = false;
}

void FGameplayDebuggerCategory_GMCAbilitySystem::CollectData(APlayerController* OwnerPC, AActor* DebugActor)
{
	if (DebugActor)
	{
		DataPack.ActorName = DebugActor->GetName();
		
		if (const UGMC_AbilitySystemComponent* AbilityComponent = DebugActor->FindComponentByClass<UGMC_AbilitySystemComponent>())
		{
			AbilityComponent->GMCMovementComponent->SV_SwapServerState();
			DataPack.GrantedAbilities = AbilityComponent->GetGrantedAbilities().ToStringSimple();
			DataPack.NBGrantedAbilities = AbilityComponent->GetGrantedAbilities().Num();
			DataPack.ActiveTags = AbilityComponent->GetActiveTags().ToStringSimple();
			DataPack.NBActiveTags = AbilityComponent->GetActiveTags().Num();
			DataPack.Attributes = AbilityComponent->GetAllAttributesString();
			DataPack.NBAttributes = AbilityComponent->GetAllAttributes().Num();
			DataPack.ActiveEffects = AbilityComponent->GetActiveEffectsString();
			DataPack.NBActiveEffects = AbilityComponent->GetActiveEffects().Num();
			DataPack.ActiveEffectData = AbilityComponent->GetActiveEffectsDataString();
			DataPack.NBActiveEffectData = AbilityComponent->ActiveEffectsData.Num();
			DataPack.ActiveAbilities = AbilityComponent->GetActiveAbilitiesString();
			DataPack.NBActiveAbilities = AbilityComponent->GetActiveAbilities().Num();
			
			AbilityComponent->GMCMovementComponent->SV_SwapServerState();
		}
	}
}

void FGameplayDebuggerCategory_GMCAbilitySystem::DrawData(APlayerController* OwnerPC,
	FGameplayDebuggerCanvasContext& CanvasContext)
{
	const AActor* LocalDebugActor = FindLocalDebugActor();
	const UGMC_AbilitySystemComponent* AbilityComponent = LocalDebugActor ? LocalDebugActor->FindComponentByClass<UGMC_AbilitySystemComponent>() : nullptr;
	if (AbilityComponent == nullptr) return;
	
	if (!DataPack.ActorName.IsEmpty())
	{
		CanvasContext.Printf(TEXT("{yellow}Actor name: {white}%s"), *DataPack.ActorName);

		constexpr int MaxCharDisplayAbilities = 100;
		// Abilities
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Granted Abilities (%d): {white}%s%s"), DataPack.NBGrantedAbilities, *DataPack.GrantedAbilities.Left(MaxCharDisplayAbilities), DataPack.GrantedAbilities.Len() > MaxCharDisplayAbilities ? TEXT("...") : TEXT(""));
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBGrantedAbilities != AbilityComponent->GetGrantedAbilities().Num())
			{
				CanvasContext.Printf(
					TEXT("{green}[client] {yellow}Granted Abilities (%d): {red} [INCOHERENCY] {white}%s%s"),
					AbilityComponent->GetGrantedAbilities().Num(),
					*AbilityComponent->GetGrantedAbilities().ToStringSimple().Left(MaxCharDisplayAbilities),
					AbilityComponent->GetGrantedAbilities().ToStringSimple().Len() > MaxCharDisplayAbilities ? TEXT("...") : TEXT(""));
			}
			else
			{
				CanvasContext.Printf(
					TEXT("{green}[client] {yellow}Granted Abilities (%d): {white}%s%s"), AbilityComponent->GetGrantedAbilities().Num(),
					*AbilityComponent->GetGrantedAbilities().ToStringSimple().Left(MaxCharDisplayAbilities),
					AbilityComponent->GetGrantedAbilities().ToStringSimple().Len() > MaxCharDisplayAbilities ? TEXT("...") : TEXT(""));
			}
		}

		// Active Abilities
		CanvasContext.Printf(TEXT("\n{blue}[server] {yellow}Active Abilities: {white}%s"), *DataPack.ActiveAbilities);
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBActiveAbilities != AbilityComponent->GetActiveAbilities().Num())
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Abilities: {red} [INCOHERENCY] {white}%s"), *AbilityComponent->GetActiveAbilitiesString());
			else
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Abilities: {white}%s"), *AbilityComponent->GetActiveAbilitiesString());
		}

		// Tags
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Tags: {white}%s"), *DataPack.ActiveTags);
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBActiveTags != AbilityComponent->GetActiveTags().Num())
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Tags: {red} [INCOHERENCY] {white}%s"), *AbilityComponent->GetActiveTags().ToStringSimple());
			else
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Tags: {white}%s"), *AbilityComponent->GetActiveTags().ToStringSimple());
		}

		// Attributes
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Attributes: {white}%s"), *DataPack.Attributes);
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBAttributes != AbilityComponent->GetAllAttributes().Num())
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Attributes: {red} [INCOHERENCY] {white}%s"), *AbilityComponent->GetAllAttributesString());
			else
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Attributes: {white}%s"), *AbilityComponent->GetAllAttributesString());
		}

		// Active Effects
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Effects: {white}%s"), *DataPack.ActiveEffects);
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBActiveEffects != AbilityComponent->GetActiveEffects().Num())
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Effects: {red} [INCOHERENCY] {white}%s"), *AbilityComponent->GetActiveEffectsString());
			else
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Effects: {white}%s"), *AbilityComponent->GetActiveEffectsString());
		}

		// Active Effects Data
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Effects Data: {white}%s"), *DataPack.ActiveEffectData);
		// Show client-side data
		if (AbilityComponent)
		{
			if (DataPack.NBActiveEffectData != AbilityComponent->ActiveEffectsData.Num())
				CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Effects Data: {red} [INCOHERENCY] {white}%s"), *AbilityComponent->GetActiveEffectsDataString());
			else
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Effects Data: {white}%s"), *AbilityComponent->GetActiveEffectsDataString());
		}
		
	}
}

TSharedRef<FGameplayDebuggerCategory> FGameplayDebuggerCategory_GMCAbilitySystem::MakeInstance()
{
	return MakeShareable(new FGameplayDebuggerCategory_GMCAbilitySystem());
}

void FGameplayDebuggerCategory_GMCAbilitySystem::FRepData::Serialize(FArchive& Ar)
{
	Ar << ActorName;
	Ar << GrantedAbilities;
	Ar << ActiveTags;
	Ar << Attributes;
	Ar << ActiveEffects;
	Ar << ActiveEffectData;
	Ar << ActiveAbilities;
	Ar << NBGrantedAbilities;
	Ar << NBActiveTags;
	Ar << NBAttributes;
	Ar << NBActiveEffects;
	Ar << NBActiveEffectData;
	Ar << NBActiveAbilities;
}

#endif