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
	if (OwnerPC)
	{
		if (OwnerPC->GetPawn())
		{
			DataPack.ActorName = OwnerPC->GetPawn()->GetName();

			if (const UGMC_AbilitySystemComponent* AbilityComponent = OwnerPC->GetPawn()->FindComponentByClass<UGMC_AbilitySystemComponent>())
			{
				DataPack.GrantedAbilities = AbilityComponent->GetGrantedAbilities().ToStringSimple();
				DataPack.ActiveTags = AbilityComponent->GetActiveTags().ToStringSimple();
				DataPack.Attributes = AbilityComponent->GetAllAttributesString();
				DataPack.ActiveEffects = AbilityComponent->GetActiveEffectsString();
				DataPack.ActiveEffectData = AbilityComponent->GetActiveEffectsDataString();
				DataPack.ActiveAbilities = AbilityComponent->GetActiveAbilitiesString();
			}
		}
		else
		{
			DataPack.ActorName = TEXT("Spectator or missing pawn");
		}
	}
}

void FGameplayDebuggerCategory_GMCAbilitySystem::DrawData(APlayerController* OwnerPC,
	FGameplayDebuggerCanvasContext& CanvasContext)
{
	if (!DataPack.ActorName.IsEmpty())
	{
		CanvasContext.Printf(TEXT("{yellow}Actor name: {white}%s"), *DataPack.ActorName);
		const UGMC_AbilitySystemComponent* AbilityComponent = OwnerPC->GetPawn() ? OwnerPC->GetPawn()->FindComponentByClass<UGMC_AbilitySystemComponent>() : nullptr;
		if (AbilityComponent == nullptr) return;

		// Abilities
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Granted Abilities: {white}%s"), *DataPack.GrantedAbilities);
		// Show client-side data
		if (AbilityComponent)
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Granted Abilities: {white}%s"), *AbilityComponent->GetGrantedAbilities().ToStringSimple());
		}

		// Active Abilities
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Abilities: {white}%s"), *DataPack.ActiveAbilities);
		// Show client-side data
		if (AbilityComponent)
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Abilities: {white}%s"), *AbilityComponent->GetActiveAbilitiesString());
		}

		// Tags
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Tags: {white}%s"), *DataPack.ActiveTags);
		// Show client-side data
		if (AbilityComponent)
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Tags: {white}%s"), *AbilityComponent->GetActiveTags().ToStringSimple());
		}

		// Attributes
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Attributes: {white}%s"), *DataPack.Attributes);
		// Show client-side data
		if (AbilityComponent)
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Attributes: {white}%s"), *AbilityComponent->GetAllAttributesString());
		}

		// Active Effects
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Effects: {white}%s"), *DataPack.ActiveEffects);
		// Show client-side data
		if (AbilityComponent)
		{
			CanvasContext.Printf(TEXT("{green}[client] {yellow}Active Effects: {white}%s"), *AbilityComponent->GetActiveEffectsString());
		}

		// Active Effects Data
		CanvasContext.Printf(TEXT("{blue}[server] {yellow}Active Effects Data: {white}%s"), *DataPack.ActiveEffectData);
		// Show client-side data
		if (AbilityComponent)
		{
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
}

#endif