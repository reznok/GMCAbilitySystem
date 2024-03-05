// Fill out your copyright notice in the Description page of Project Settings.

#pragma once
#include "CoreMinimal.h"
#include "GameplayDebuggerCategory.h"
#include "GameplayTagContainer.h"
#if WITH_GAMEPLAY_DEBUGGER

/**
 * 
 */

class GMCABILITYSYSTEM_API FGameplayDebuggerCategory_GMCAbilitySystem : public FGameplayDebuggerCategory
{
public:
	FGameplayDebuggerCategory_GMCAbilitySystem();
	virtual void CollectData(APlayerController* OwnerPC, AActor* DebugActor) override;
	virtual void DrawData(APlayerController* OwnerPC, FGameplayDebuggerCanvasContext& CanvasContext) override;

	static TSharedRef<FGameplayDebuggerCategory> MakeInstance();

protected:
	struct FRepData
	{
		// Put all data you want to display here
		FString ActorName;
		FString GrantedAbilities;
		FString ActiveTags;
		FString Attributes;
		FString ActiveEffects;
		FString ActiveEffectData;
		FString ActiveAbilities;
        
		void Serialize(FArchive& Ar);
	};
    
	FRepData DataPack;
};

#endif	