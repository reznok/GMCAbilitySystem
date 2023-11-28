#include "GMCAbility.h"
#include "Components/GMCAbilityComponent.h"

bool FGMCAbilityData::operator==(FGMCAbilityData const& Other) const
{
	return AbilityActivationID == Other.AbilityActivationID && GrantedAbilityIndex == Other.GrantedAbilityIndex;
}

FString FGMCAbilityData::ToStringSimple() const
{
	FString RetString = FString::Printf(TEXT("ID: %d"), AbilityActivationID);
	return RetString;
}

void UGMCAbility::Execute(FGMCAbilityData AbilityData, UGMC_AbilityComponent* InAbilityComponent)
{
	this->AbilityComponent = InAbilityComponent;
	BeginAbility(AbilityData);
}

void UGMCAbility::CommitAbilityCost()
{
	if (AbilityComponent)
	{
		AbilityComponent->ApplyAbilityCost(this);
	}
}

void UGMCAbility::BeginAbility_Implementation(FGMCAbilityData AbilityData)
{
	UE_LOG(LogTemp, Warning, TEXT("Default Execute Method"));
}
