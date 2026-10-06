// Layer 2: client-side confirmation timeouts run on the component's confirm clock, not on ActionTimer.
// ActionTimer is the GMC move timestamp; it re-bases to the server's clock when a joining client's
// first moves are acknowledged, so a start stamped before the jump must not age by the jump.

#include "Misc/AutomationTest.h"
#include "Components/GMCAbilityComponent.h"
#include "Ability/GMCAbility.h"
#include "Effects/GMCAbilityEffect.h"
#include "UGMAS_TestMovementCmp.h"
#include "UGMAS_TestDelayAbility.h"

#if WITH_DEV_AUTOMATION_TESTS

// UGMCAbility::ServerConfirmTimeout (2 s) plus margin.
constexpr double kPastConfirmTimeout = 2.5;

BEGIN_DEFINE_SPEC(FGMASConfirmClockSpec,
	"GMAS.Unit.BugFix.ConfirmClock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;

	UGMCAbility* FirstActiveAbility() const;

END_DEFINE_SPEC(FGMASConfirmClockSpec)

UGMCAbility* FGMASConfirmClockSpec::FirstActiveAbility() const
{
	for (const TPair<int, UGMCAbility*>& Pair : AbilityComp->GetActiveAbilities())
	{
		return Pair.Value;
	}
	return nullptr;
}

void FGMASConfirmClockSpec::Define()
{
	BeforeEach([this]()
	{
		MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
		MoveCmp->AddToRoot();
		AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
		AbilityComp->AddToRoot();
		AbilityComp->GMCMovementComponent = MoveCmp;
		AbilityComp->BindReplicationData();
		// Near zero: a client's clock right after joining.
		AbilityComp->SetActionTimerForTest(0.037);
		GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 1.0e7f;   // outlasts every ActionTimer jump below
	});

	AfterEach([this]()
	{
		GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 0.2f;
		AbilityComp->RemoveFromRoot();
		MoveCmp->RemoveFromRoot();
		AbilityComp = nullptr; MoveCmp = nullptr;
	});

	Describe("Unconfirmed ability", [this]()
	{
		It("survives an ActionTimer jump to the server clock", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->TickActiveAbilitiesForTest(0.016f);
			TestEqual("still running after the clock jump", Ability->AbilityState, EAbilityState::Initialized);
		});

		It("is cancelled once the confirm clock passes ServerConfirmTimeout", [this]()
		{
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedMessagePlain(TEXT("[AbilityCut] Ability ending with"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->AdvanceConfirmClockForTest(kPastConfirmTimeout);
			AbilityComp->TickActiveAbilitiesForTest(0.016f);
			TestEqual("cancelled by the confirm timeout", Ability->AbilityState, EAbilityState::Ended);
		});

		It("stays when confirmed before the timeout", [this]()
		{
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			Ability->ServerConfirm();
			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->AdvanceConfirmClockForTest(kPastConfirmTimeout);
			AbilityComp->TickActiveAbilitiesForTest(0.016f);
			TestEqual("still running", Ability->AbilityState, EAbilityState::Initialized);
		});

		It("stays when the server's Confirmed answer for its operation arrives before the timeout", [this]()
		{
			constexpr int OpID = -5;
			const int AbilityID = UGMC_AbilitySystemComponent::DeriveAbilityIDFromOperationForTest(OpID, 0);
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass(), nullptr, FGameplayTag::EmptyTag,
				false, AbilityID, OpID, /*SourceCandidateIndex=*/0);
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			FGMASAbilitySyncMessage Answer;
			Answer.Type = EGMASAbilitySyncType::Answer;
			Answer.OperationID = OpID;
			Answer.AbilityID = AbilityID;
			Answer.Answer = EGMASAbilityAnswer::Confirmed;
			AbilityComp->ReceiveAbilitySyncForTest(Answer);
			TestTrue("confirmed by the answer", Ability->IsServerConfirmed());

			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->AdvanceConfirmClockForTest(kPastConfirmTimeout);
			AbilityComp->TickActiveAbilitiesForTest(0.016f);
			TestEqual("still running", Ability->AbilityState, EAbilityState::Initialized);
		});
	});

	Describe("Pending predicted effect", [this]()
	{
		auto ApplyPending = [this](UGMCAbilityEffect*& OutEffect) -> int
		{
			OutEffect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			OutEffect->AddToRoot();
			FGMCAbilityEffectData Data;
			Data.EffectType = EGMASEffectType::Persistent;
			Data.Duration   = 0.f;
			AbilityComp->ApplyAbilityEffect(OutEffect, Data);
			const int Id = OutEffect->EffectData.EffectID;
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Pending);
			AbilityComp->BoundActiveEffectIDs_Remove(Id);   // the server never answered
			return Id;
		};

		It("survives an ActionTimer jump to the server clock", [this, ApplyPending]()
		{
			UGMCAbilityEffect* Effect = nullptr;
			const int Id = ApplyPending(Effect);
			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->TickActiveEffects(0.f);
			TestTrue("still active", AbilityComp->GetActiveEffects().Contains(Id));
			TestEqual("still pending",
				static_cast<int>(AbilityComp->GetProcessedEffectIDsForTest().FindRef(Id)),
				static_cast<int>(EGMCEffectAnswerState::Pending));
			Effect->RemoveFromRoot();
		});

		It("is reaped once the confirm clock passes the application timeout", [this, ApplyPending]()
		{
			AddExpectedErrorPlain(TEXT("Not Confirmed By Server"), EAutomationExpectedErrorFlags::Contains, 1);
			UGMCAbilityEffect* Effect = nullptr;
			const int Id = ApplyPending(Effect);
			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->AdvanceConfirmClockForTest(5.0);
			AbilityComp->TickActiveEffects(0.f);
			TestFalse("reaped", AbilityComp->GetActiveEffects().Contains(Id));
			Effect->RemoveFromRoot();
		});

		It("stays when confirmed before the timeout", [this, ApplyPending]()
		{
			UGMCAbilityEffect* Effect = nullptr;
			const int Id = ApplyPending(Effect);
			AbilityComp->GetProcessedEffectIDsForTest().Add(Id, EGMCEffectAnswerState::Validated);
			AbilityComp->SetActionTimerForTest(3006.326);
			AbilityComp->AdvanceConfirmClockForTest(5.0);
			AbilityComp->TickActiveEffects(0.f);
			TestTrue("still active", AbilityComp->GetActiveEffects().Contains(Id));
			Effect->RemoveFromRoot();
		});
	});

	Describe("Clock", [this]()
	{
		It("does not advance within one frame and caps each step", [this]()
		{
			const double A = AbilityComp->GetConfirmClock();
			const double B = AbilityComp->GetConfirmClock();
			TestEqual("same frame, same value", A, B);
			TestTrue("step cap is below the confirm timeout", UGMC_AbilitySystemComponent::MaxConfirmClockStep < 1.0);
		});
	});
}

#endif // WITH_DEV_AUTOMATION_TESTS
