#pragma once
#include "GMCAbilityEffect.h"
#include "GMCAbility.generated.h"

USTRUCT(BlueprintType)
struct FGMCAbilityData
{
	GENERATED_USTRUCT_BODY()

	FGMCAbilityData()
	{
		GrantedAbilityIndex = -1;
		AbilityActivationID = AbilityActivationIDCounter++;
		TargetVector0 = FVector::Zero();
		TargetVector1 = FVector::Zero();
		TargetVector2 = FVector::Zero();
		TargetActor = nullptr;
		TargetComponent = nullptr;
	}

	FGMCAbilityData(FGMCAbilityData const& Other)
	{
		*this = Other;
	}
	
	bool operator==(FGMCAbilityData const& Other) const;
	FString ToStringSimple() const;

	// GUID to reference the specific ability data
	// https://forums.unrealengine.com/t/fs-test-id-is-not-initialized-properly/560690/5 for why the meta is needed
	UPROPERTY(BlueprintReadOnly, meta = (IgnoreForMemberInitializationTest))
	int AbilityActivationID;

	inline static int AbilityActivationIDCounter;
	
	// Ability ID to cast
	UPROPERTY(BlueprintReadWrite)
	int GrantedAbilityIndex;

	// Generic targeting data that can be used for anything
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector0;
	
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector1;
	
	UPROPERTY(BlueprintReadWrite)
	FVector TargetVector2;

	// Target Actor
	UPROPERTY(BlueprintReadWrite)
	AActor* TargetActor;

	// Target Component
	UPROPERTY(BlueprintReadWrite)
	UActorComponent* TargetComponent;
	
	bool bProcessed = true;
	
};

// Forward Declarations
class UGMC_AbilityComponent;

UCLASS(Blueprintable, BlueprintType)
class GMCABILITYSYSTEM_API UGMCAbility : public UObject
{
	GENERATED_BODY()

public:
	void Execute(FGMCAbilityData AbilityData, UGMC_AbilityComponent* InAbilityComponent);
	
	UFUNCTION(BlueprintNativeEvent)
	void BeginAbility(FGMCAbilityData AbilityData);

	UPROPERTY(EditAnywhere)
	TSubclassOf<UGMCAbilityEffect> AbilityCost;

	UFUNCTION(BlueprintCallable)
	void CommitAbilityCost();

	UPROPERTY(BlueprintReadOnly)
	UGMC_AbilityComponent* AbilityComponent;
};


