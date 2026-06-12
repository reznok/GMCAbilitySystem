// Chain (combo) tests: ChainWindowTag / ChainWindowDuration /
// ChainConsumeWindowTags on UGMCAbility, plus first-passing-wins selection in
// TryActivateAbilitiesByInputTag.
//
// Semantics under test:
//   - Natural EndAbility applies a transient effect granting ChainWindowTag
//     for ChainWindowDuration (CancelAbility does NOT).
//   - Successful activation removes effects matching ChainConsumeWindowTags
//     (a tag-gate-denied activation consumes nothing).
//   - Window expiry (effect duration) drops the tag -> chain resets.
//   - One InputTag with multiple abilities activates only the first whose
//     gates pass.
//
// Harness mirrors GMAS_ActivationSpec: transient MovementCmp + ASC, two
// ability stub classes (A = stage 1, B = stage 2), CDOs reset per test.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/GMCAbilityComponent.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestAbility.h"
#include "UGMAS_TestAbilityB.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASChainSpec,
	"GMAS.Unit.Chain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;

	FGameplayTag AbilityTagA;   // GMAS.Test.Ability.Attack
	FGameplayTag AbilityTagB;   // GMAS.Test.Ability.Defend
	FGameplayTag Window2Tag;    // GMAS.Test.Combo.Window2
	FGameplayTag BurningTag;    // GMAS.Test.Status.Burning
	FGameplayTag InputTag;      // GMAS.Test.Input.Chain

	double SimTime = 0.0;

	void SetupHarness();
	void TeardownHarness();
	void AdvanceTime(float Dt);
	UGMCAbility* FindActiveAbilityByTag(const FGameplayTag& Tag) const;

END_DEFINE_SPEC(FGMASChainSpec)

void FGMASChainSpec::SetupHarness()
{
	static FNativeGameplayTag SAbilityTagA(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Ability.Attack"), TEXT("Attack ability tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SAbilityTagB(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Ability.Defend"), TEXT("Defend ability tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SWindow2Tag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Combo.Window2"), TEXT("Chain window 2 for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Burning"), TEXT("Burning status for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SInputTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Input.Chain"), TEXT("Chain input tag for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	AbilityTagA = SAbilityTagA.GetTag();
	AbilityTagB = SAbilityTagB.GetTag();
	Window2Tag  = SWindow2Tag.GetTag();
	BurningTag  = SBurningTag.GetTag();
	InputTag    = SInputTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = -1.0;
	SimTime = 0.0;

	// A = stage 1: opens Window2 on natural end.
	UGMCAbility* CDOA = GetMutableDefault<UGMAS_TestAbility>();
	CDOA->AbilityTag              = AbilityTagA;
	CDOA->CooldownTime            = 0.f;
	CDOA->bAllowMultipleInstances = true;
	CDOA->ChainWindowTag          = Window2Tag;
	CDOA->ChainWindowDuration     = 2.5f;

	// B = stage 2: requires + consumes Window2.
	UGMCAbility* CDOB = GetMutableDefault<UGMAS_TestAbilityB>();
	CDOB->AbilityTag              = AbilityTagB;
	CDOB->CooldownTime            = 0.f;
	CDOB->bAllowMultipleInstances = true;
	CDOB->ActivationRequiredTags.AddTag(Window2Tag);
	CDOB->ChainConsumeWindowTags.AddTag(Window2Tag);
}

void FGMASChainSpec::TeardownHarness()
{
	auto ResetCDO = [](UGMCAbility* CDO)
	{
		CDO->AbilityTag              = FGameplayTag();
		CDO->CooldownTime            = 0.f;
		CDO->bAllowMultipleInstances = false;
		CDO->ActivationRequiredTags  = FGameplayTagContainer();
		CDO->ActivationBlockedTags   = FGameplayTagContainer();
		CDO->BlockOtherAbility       = FGameplayTagContainer();
		CDO->BlockedByOtherAbility   = FGameplayTagContainer();
		CDO->CancelAbilitiesWithTag  = FGameplayTagContainer();
		CDO->bBlockAllOtherAbilities = false;
		CDO->BlockAllAllowedTags     = FGameplayTagContainer();
		CDO->ChainWindowTag          = FGameplayTag();
		CDO->ChainWindowDuration     = 0.f;
		CDO->ChainConsumeWindowTags  = FGameplayTagContainer();
	};
	ResetCDO(GetMutableDefault<UGMAS_TestAbility>());
	ResetCDO(GetMutableDefault<UGMAS_TestAbilityB>());

	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AbilityComp = nullptr;
	MoveCmp     = nullptr;
}

void FGMASChainSpec::AdvanceTime(float Dt)
{
	// GenPredictionTick overwrites ActionTimer from the stub's move timestamp
	// (private, always 0), so simulated time advances manually and effects
	// tick through the public TickActiveEffects seam. The zero-dt prediction
	// tick first handles ended-ability cleanup; ancillary covers cooldowns.
	AbilityComp->GenPredictionTick(0.f);
	SimTime += Dt;
	AbilityComp->ActionTimer = SimTime;
	AbilityComp->TickActiveEffects(Dt);
	AbilityComp->GenAncillaryTick(Dt, /*bIsCombinedClientMove=*/false);
}

UGMCAbility* FGMASChainSpec::FindActiveAbilityByTag(const FGameplayTag& Tag) const
{
	for (const auto& Pair : AbilityComp->GetActiveAbilities())
	{
		if (Pair.Value && Pair.Value->AbilityTag == Tag && Pair.Value->AbilityState != EAbilityState::Ended)
		{
			return Pair.Value;
		}
	}
	return nullptr;
}

void FGMASChainSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("ChainWindow grant", [this]()
	{
		It("natural EndAbility grants the window tag", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);
			TestTrue("Window2 granted after natural end", AbilityComp->HasActiveTag(Window2Tag));
		});

		It("CancelAbility grants no window", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			if (UGMCAbility* A = FindActiveAbilityByTag(AbilityTagA))
			{
				A->CancelAbility();
			}
			AdvanceTime(0.f);
			TestFalse("no window after cancel", AbilityComp->HasActiveTag(Window2Tag));
		});
	});

	Describe("Stage progression", [this]()
	{
		It("stage 2 activates inside the window and consumes it", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);

			const bool bB = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestTrue("B activated inside window", bB);
			TestEqual("B active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
			AdvanceTime(0.f);
			TestFalse("window consumed by B", AbilityComp->HasActiveTag(Window2Tag));
		});

		It("stage 2 cannot double-fire after consuming the window", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);

			AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			AdvanceTime(0.f);
			const bool bSecond = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestFalse("second B denied (window gone)", bSecond);
			TestEqual("only one B", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
		});

		It("stage 2 is denied with no window at all", [this]()
		{
			const bool bB = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestFalse("B denied without window", bB);
		});
	});

	Describe("Window expiry", [this]()
	{
		It("window expires after ChainWindowDuration and stage 2 is denied", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);
			TestTrue("window open initially", AbilityComp->HasActiveTag(Window2Tag));

			AdvanceTime(3.0f); // > 2.5s duration
			TestFalse("window expired", AbilityComp->HasActiveTag(Window2Tag));
			TestFalse("B denied after expiry", AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass()));
		});
	});

	Describe("Stage-1 blocking", [this]()
	{
		It("stage 1 is blocked while its own window is open", [this]()
		{
			GetMutableDefault<UGMAS_TestAbility>()->ActivationBlockedTags.AddTag(Window2Tag);

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);

			const bool bAgain = AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			TestFalse("A denied while Window2 open", bAgain);
		});
	});

	Describe("Denied activation", [this]()
	{
		It("a tag-gate-denied activation consumes no window", [this]()
		{
			GetMutableDefault<UGMAS_TestAbilityB>()->ActivationBlockedTags.AddTag(BurningTag);

			AbilityComp->TryActivateAbility(UGMAS_TestAbility::StaticClass());
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);

			AbilityComp->AddActiveTag(BurningTag);
			const bool bB = AbilityComp->TryActivateAbility(UGMAS_TestAbilityB::StaticClass());
			TestFalse("B denied by blocked tag", bB);
			TestTrue("window survives the denied press", AbilityComp->HasActiveTag(Window2Tag));
		});
	});

	Describe("First-passing-wins", [this]()
	{
		It("one InputTag press activates only the first ability whose gates pass", [this]()
		{
			// A is stage 1 (blocked while Window2 open), B is stage 2.
			GetMutableDefault<UGMAS_TestAbility>()->ActivationBlockedTags.AddTag(Window2Tag);

			FAbilityMapData MapData;
			MapData.InputTag = InputTag;
			MapData.Abilities.Add(UGMAS_TestAbility::StaticClass());
			MapData.Abilities.Add(UGMAS_TestAbilityB::StaticClass());
			MapData.bGrantedByDefault = true;
			AbilityComp->AddAbilityMapData(MapData);

			// No window: press activates A only.
			AbilityComp->TryActivateAbilitiesByInputTag(InputTag, nullptr);
			TestEqual("press 1: A active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 1);
			TestEqual("press 1: no B", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 0);

			// Open the window, then press again: A is blocked, B activates.
			AbilityComp->EndAbilitiesByTag(AbilityTagA);
			AdvanceTime(0.f);
			AbilityComp->TryActivateAbilitiesByInputTag(InputTag, nullptr);
			TestEqual("press 2: B active", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbilityB::StaticClass()), 1);
			TestEqual("press 2: A still ended", AbilityComp->GetActiveAbilityCount(UGMAS_TestAbility::StaticClass()), 0);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
