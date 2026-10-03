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
		int NBGrantedAbilities;
		// Split tag display: bound is GMC-validated (server vs client divergence = real bug),
		// client-auth is locally maintained (eventually consistent, divergence is normal during
		// BoundQueueV2 propagation).
		FString BoundActiveTags;
		int NBBoundActiveTags;
		FString ClientAuthActiveTags;
		int NBClientAuthActiveTags;
		FString Attributes;
		int NBAttributes;
		FString ActiveEffects;
		int NBActiveEffects;
		FString ActiveEffectData;
		int NBActiveEffectData;
		FString ActiveAbilities;
		int NBActiveAbilities;

		int NBCachedOperationPayloads;
        
		void Serialize(FArchive& Ar);
	};
    
	FRepData DataPack;
};

#endif	