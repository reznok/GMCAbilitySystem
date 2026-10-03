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
#include "UGMAS_TestTagWatchAbility.h"
#include "Ability/Tasks/SetTargetDataFloat.h"
#include "Ability/Tasks/SetTargetDataInt.h"
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
	FGameplayTag WatchedTag;

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
	static FNativeGameplayTag SWatchedTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Watched"), TEXT("Tag watched by WaitForGameplayTagChange tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	WatchedTag = SWatchedTag.GetTag();

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
	GetMutableDefault<UGMAS_TestTagWatchAbility>()->WatchTag = FGameplayTag();
	GetMutableDefault<UGMAS_TestTagWatchAbility>()->WatchType = EGMCWaitForGameplayTagChangeType::Set;
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

		It("an unconfirmed client instance is cancelled after ServerConfirmTimeout", [this]()
		{
			AbilityComp->bForceAuthorityForTest = false;
			// Plain match: AddExpectedError treats its pattern as a regex, where "[AbilityCut]" is a
			// character class and would never match the logged line.
			AddExpectedErrorPlain(TEXT("[AbilityCut] Client cancelling unconfirmed ability"), EAutomationExpectedErrorFlags::Contains, 1);
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

	Describe("Payload type checks", [this]()
	{
		It("a bound payload that is not a task struct is dropped with one Error", [this]()
		{
			AddExpectedErrorPlain(TEXT("not a FGMCAbilityTaskData"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = true;
			GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 10.f;
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			AbilityComp->QueueTaskData(FInstancedStruct::Make(FGMCAbilityEffectData{}));
			AbilityComp->PreLocalMoveExecution();          // moves the queued payload into the bound slot
			AbilityComp->GenPredictionTick(0.f);           // dispatch: dropped, not read as a task payload
			TestEqual("ability still running", AbilityComp->GetActiveAbilityCount(UGMAS_TestDelayAbility::StaticClass()), 1);
		});

		It("a SetTargetData task given another task's payload struct drops it with one Error and ends", [this]()
		{
			AddExpectedErrorPlain(TEXT("expected FGMCAbilityTaskTargetDataFloat; dropped"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = true;
			GetMutableDefault<UGMAS_TestDelayAbility>()->DelayTime = 10.f;
			AbilityComp->TryActivateAbility(UGMAS_TestDelayAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			UGMCAbilityTask_SetTargetDataFloat* Task = UGMCAbilityTask_SetTargetDataFloat::SetTargetDataFloat(Ability, 3.f);
			Task->ReadyForActivation();
			if (!TestTrue("task registered", Ability->RunningTasks.FindRef(Task->TaskID) == Task)) { return; }

			FGMCAbilityTaskTargetDataInt Wrong;
			Wrong.TaskType = EGMCAbilityTaskDataType::Progress;
			Wrong.AbilityID = Ability->GetAbilityID();
			Wrong.TaskID = Task->TaskID;
			Wrong.Target = 7;
			Ability->HandleTaskData(Task->TaskID, FInstancedStruct::Make(Wrong));
			TestTrue("task ended", Task->GetState() == EGameplayTaskState::Finished);
		});
	});

	Describe("WaitForGameplayTagChange", [this]()
	{
		It("completes once on the watched tag and ignores a later change", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			GetMutableDefault<UGMAS_TestTagWatchAbility>()->WatchTag = WatchedTag;
			GetMutableDefault<UGMAS_TestTagWatchAbility>()->WatchType = EGMCWaitForGameplayTagChangeType::Set;
			AbilityComp->TryActivateAbility(UGMAS_TestTagWatchAbility::StaticClass());
			UGMCAbility* Ability = FirstActiveAbility();
			if (!TestNotNull("instance", Ability)) { return; }

			AbilityComp->AddActiveTag(WatchedTag);
			AbilityComp->GenAncillaryTick(0.f, false);     // CheckActiveTagsChanged -> delegate -> task completes -> ability ends
			TestEqual("ended on the tag", Ability->AbilityState, EAbilityState::Ended);
			AbilityComp->CleanupStaleAbilitiesForTest();

			// A second change must reach nobody: the binding was removed in OnDestroy.
			AbilityComp->RemoveActiveTag(WatchedTag);
			AbilityComp->GenAncillaryTick(0.f, false);
			AbilityComp->AddActiveTag(WatchedTag);
			AbilityComp->GenAncillaryTick(0.f, false);
			TestEqual("no new instance, no crash", AbilityComp->GetActiveAbilities().Num(), 0);
		});
	});

	Describe("RemoveFilteredTagChangeDelegate", [this]()
	{
		It("removes by handle whatever container is passed", [this]()
		{
			int Calls = 0;
			const FDelegateHandle Handle = AbilityComp->AddFilteredTagChangeDelegate(FGameplayTagContainer(WatchedTag),
				FGameplayTagFilteredMulticastDelegate::FDelegate::CreateLambda([&Calls](const FGameplayTagContainer&, const FGameplayTagContainer&) { Calls++; }));
			AbilityComp->RemoveFilteredTagChangeDelegate(FGameplayTagContainer(), Handle);   // different container
			AbilityComp->AddActiveTag(WatchedTag);
			AbilityComp->GenAncillaryTick(0.f, false);
			TestEqual("not called after removal", Calls, 0);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
