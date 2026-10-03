// Layer 2: abilities with tasks driven through the test seams (controlled ActionTimer,
// real TickActiveAbilities). Covers WaitDelay, the bound-attribute helper ability and the
// confirm-timeout authority seam.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestDelayAbility.h"
#include "UGMAS_TestBoundAttrAbility.h"
#include "GMAS_TestHelpers.h"

#if WITH_AUTOMATION_WORKER

// Arithmetic origin of every clock value in this spec (not an id-range choice).
constexpr double kStart = 1.0;

BEGIN_DEFINE_SPEC(FGMASTaskSpec,
	"GMAS.Unit.Task",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;
	UGMCAttributesData*          AttrData    = nullptr;
	FGameplayTag StaminaTag;

	void SetupHarness();
	void TeardownHarness();
	UGMCAbility* FirstActiveAbility() const;

END_DEFINE_SPEC(FGMASTaskSpec)

void FGMASTaskSpec::SetupHarness()
{
	static FNativeGameplayTag SStaminaTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Stamina"), TEXT("Stamina for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	StaminaTag = SStaminaTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();
	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData Stamina;
	Stamina.AttributeTag = StaminaTag;
	Stamina.DefaultValue = 100.f;
	Stamina.Clamp.Min = 0.f;
	Stamina.Clamp.Max = 200.f;
	Stamina.bGMCBound = true;
	AttrData->AttributeData.Add(Stamina);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(kStart);

	GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 0.5f;
}

void FGMASTaskSpec::TeardownHarness()
{
	GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 0.2f;
	GetMutableDefault<UGMAS_TestBoundAttrAbility>()->StaminaMod = 25.f;
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

UGMCAbility* FGMASTaskSpec::FirstActiveAbility() const
{
	for (const TPair<int, UGMCAbility*>& Pair : AbilityComp->GetActiveAbilities())
	{
		if (Pair.Value) { return Pair.Value; }
	}
	return nullptr;
}

void FGMASTaskSpec::Define()
{
	BeforeEach([this]() { SetupHarness(); });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("WaitDelay through the seams", [this]()
	{
		It("completes when ActionTimer passes TimeStarted + Time and the ability ends", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			TestTrue("activated", AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass()));
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }
			TestEqual("one running task", Ability->RunningTasks.Num(), 1);

			AbilityComp->SetActionTimerForTest(kStart + 0.25);
			AbilityComp->TickActiveAbilitiesForTest(0.25f);
			TestEqual("still running before the delay elapses", Ability->AbilityState, EAbilityState::Initialized);

			// WaitDelay completes when TimeStarted + Time <= ActionTimer: the inclusive boundary is intentional.
			AbilityComp->SetActionTimerForTest(kStart + 0.5);
			AbilityComp->TickActiveAbilitiesForTest(0.25f);
			TestEqual("ended once the delay elapsed", Ability->AbilityState, EAbilityState::Ended);
			TestEqual("task unregistered", Ability->RunningTasks.Num(), 0);

			AbilityComp->CleanupStaleAbilitiesForTest();
			TestEqual("instance purged", AbilityComp->GetActiveAbilities().Num(), 0);
		});
	});

	Describe("Confirm-timeout authority seam", [this]()
	{
		It("does not die of [AbilityCut] after ServerConfirmTimeout when authority is forced", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 10.f;
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			for (int Step = 1; Step <= 6; ++Step)
			{
				AbilityComp->SetActionTimerForTest(kStart + Step * 0.5);   // reaches kStart + 3.0 > kStart + ServerConfirmTimeout 2.0
				AbilityComp->TickActiveAbilitiesForTest(0.5f);
			}
			TestEqual("still running past the confirm timeout", Ability->AbilityState, EAbilityState::Initialized);
		});

		It("an unconfirmed client instance is removed after ServerConfirmTimeout", [this]()
		{
			AbilityComp->bForceAuthorityForTest = false;
			// Plain match: AddExpectedError treats its pattern as a regex, where "[AbilityCut]" is a
			// character class and would never match the logged line.
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client removing unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedMessagePlain(TEXT("[AbilityCut] Ability ending with"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 10.f;
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			AbilityComp->SetActionTimerForTest(kStart + 2.5);   // ClientStartTime kStart + ServerConfirmTimeout 2.0 < kStart + 2.5
			AbilityComp->TickActiveAbilitiesForTest(0.5f);
			TestEqual("ended by the confirm timeout", Ability->AbilityState, EAbilityState::Ended);
		});
	});

	Describe("Bound-attribute helper ability", [this]()
	{
		It("applies +StaminaMod to RawValue and ends in the same activation", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			GetMutableDefault<UGMAS_TestBoundAttrAbility>()->StaminaMod = 25.f;
			AbilityComp->TryActivateAbility(UGMAS_TestBoundAttrAbility::StaticClass());
			AbilityComp->ProcessAttributes(true);
			TestEqual("RawValue 125", AbilityComp->GetAttributeRawValue(StaminaTag), 125.f);
			TestEqual("no live instance", AbilityComp->GetActiveAbilityCount(UGMAS_TestBoundAttrAbility::StaticClass()), 0);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
