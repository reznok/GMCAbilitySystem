#include "Attributes/GMCAttributesData.h"
#include "Ability/GMCAbility.h"
#include "Fixtures/UAbilityCostEffect.h"
#include "Misc/AutomationTest.h"

BEGIN_DEFINE_SPEC(AbilityTestsSpec, "GMCAbilitySystem.AbilityTestsSpec", EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask)
	UGMC_AbilitySystemComponent* AbilitySystemComponent;
	TMap<FName, FGameplayTag> TagMap;
END_DEFINE_SPEC(AbilityTestsSpec)
void AbilityTestsSpec::Define()
{
	Describe("CanAffordAbilityCost()", [this]()
	{
		BeforeEach([this]()
		{
			TagMap = CreateTagMap();
			
			// Attributes
			UGMCAttributesData* AttributesData = NewObject<UGMCAttributesData>();
			AttributesData->AttributeData.Add({
				TagMap[FName("Attribute.Points")],
				0.f,
				FAttributeClamp(),
				true,
			});

			// GMCMovementComponent
			UGMC_MovementUtilityCmp* MovementComponent = NewObject<UGMC_MovementUtilityCmp>();
			
			// Ability System Component
			AbilitySystemComponent = NewObject<UGMC_AbilitySystemComponent>();
			AbilitySystemComponent->GMCMovementComponent = MovementComponent;
			AbilitySystemComponent->AttributeDataAssets.Add(AttributesData);

			AbilitySystemComponent->BindReplicationData();
		});
		
		It("should return true if there is no cost defined", [this]()
		{
			// Arrange
			UGMCAbility* Ability = NewObject<UGMCAbility>();
			Ability->OwnerAbilityComponent = AbilitySystemComponent;
			Ability->AbilityCost = nullptr;

			// Act
			const bool bCanAfford = Ability->CanAffordAbilityCost();

			// Assert
			TestTrue("Can afford ability cost", bCanAfford);
		});

		It("should return false if the cost cannot be afforded", [this]()
		{	
			// Arrange
			
			UGMCAbility* Ability = NewObject<UGMCAbility>();
			Ability->OwnerAbilityComponent = AbilitySystemComponent;
			Ability->AbilityCost = UAbilityCostEffect::StaticClass();

			// Act
			const bool bCanAfford = Ability->CanAffordAbilityCost();

			// Assert
			TestFalse("Cannot afford ability cost", bCanAfford);
		});

		It("should return true if the cost can be afforded", [this]()
		{	
			// Arrange
			UGMCAbility* Ability = NewObject<UGMCAbility>();
			Ability->OwnerAbilityComponent = AbilitySystemComponent;
			Ability->AbilityCost = UAbilityCostEffect::StaticClass();
			AbilitySystemComponent->ApplyAbilityEffectModifier({
				TagMap[FName("Attribute.Points")],
				20.f,
				EModifierType::Add,
			}, true, false);

			// Act
			const bool bCanAfford = Ability->CanAffordAbilityCost();

			// Assert
			TestTrue("Can afford ability cost", bCanAfford);
		});
	});
}