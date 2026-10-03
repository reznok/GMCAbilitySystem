// Effect lifecycle specs: the grace deferral on removal. Headless UGMAS_TestMovementCmp +
// UGMC_AbilitySystemComponent harness, no world, no network stack; the networked branch of
// RemoveActiveAbilityEffect is reached through the bForceNetworkedForTest seam.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "UGMAS_TestMovementCmp.h"
#include "GMAS_TestHelpers.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASEffectLifecycleSpec,
	"GMAS.Unit.EffectLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;

	FGameplayTag HealthTag;
	FGameplayTag DrainTag;

	// The project default the cases overwrite; restored by TeardownHarness.
	float SavedDefaultClientGraceTime;

	void SetupHarness();
	void TeardownHarness();

	FGMCAttributeModifier MakeHealthMod(float Amount) const;

END_DEFINE_SPEC(FGMASEffectLifecycleSpec)

// ---------------------------------------------------------------------------
// Harness
// ---------------------------------------------------------------------------

void FGMASEffectLifecycleSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Attribute.Health"), TEXT("Health for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SDrainTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Lifecycle.Effect.Drain"), TEXT("Drain effect tag for effect-lifecycle tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag = SHealthTag.GetTag();
	DrainTag  = SDrainTag.GetTag();

	SavedDefaultClientGraceTime = GetDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime;

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData Health;
	Health.AttributeTag = HealthTag;
	Health.DefaultValue = 100.f;
	Health.Clamp.Min    = 0.f;
	Health.Clamp.Max    = 200.f;
	Health.bGMCBound    = true;
	AttrData->AttributeData.Add(Health);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(GMASTest::StableActionTimer);
}

void FGMASEffectLifecycleSpec::TeardownHarness()
{
	GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = SavedDefaultClientGraceTime;

	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

FGMCAttributeModifier FGMASEffectLifecycleSpec::MakeHealthMod(float Amount) const
{
	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	return Mod;
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

void FGMASEffectLifecycleSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	// RemoveActiveAbilityEffect on a networked Ticking/Periodic effect arms EndAtActionTimer =
	// ActionTimer + grace instead of ending the effect, so client and server fire the same number
	// of applications before EndEffect. The grace is the per-effect ClientGraceTime when > 0, else
	// the project default; 0 means no deferral.
	Describe("Grace deferral on removal", [this]()
	{
		It("networked Ticking removal arms EndAtActionTimer = ActionTimer + grace and ends when the clock reaches it", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("not ended yet", Applied->bCompleted);
			TestEqual("armed at 1.5", Applied->EndAtActionTimer, 1.5);
			AbilityComp->SetActionTimerForTest(1.25); AbilityComp->TickActiveEffects(0.25f);
			TestFalse("still alive at 1.25", Applied->bCompleted);
			AbilityComp->SetActionTimerForTest(1.5); AbilityComp->TickActiveEffects(0.25f);
			TestTrue("ended at 1.5", Applied->bCompleted);
			Effect->RemoveFromRoot();
		});

		It("a Periodic removal arms the same way", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Periodic; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("not ended yet", Applied->bCompleted);
			TestEqual("armed at 1.5", Applied->EndAtActionTimer, 1.5);
			Effect->RemoveFromRoot();
		});

		It("a Persistent removal is never deferred", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});

		It("a project grace of 0 ends the effect at once", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});

		It("a per-effect ClientGraceTime defers even when the project default is 0", [this]()
		{
			AbilityComp->bForceNetworkedForTest = true;
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.f;
			AbilityComp->SetActionTimerForTest(1.0);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.ClientGraceTime = 0.25f; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestFalse("deferred", Applied->bCompleted);
			TestEqual("armed at 1.25", Applied->EndAtActionTimer, 1.25);
			Effect->RemoveFromRoot();
		});

		It("standalone never defers", [this]()
		{
			GetMutableDefault<UGMASNetworkTimingSettings>()->DefaultClientGraceTime = 0.5f;
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage()); Effect->AddToRoot();
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Ticking; Data.EffectTag = DrainTag; Data.Modifiers.Add(MakeHealthMod(-10.f));
			UGMCAbilityEffect* Applied = AbilityComp->ApplyAbilityEffect(Effect, Data);
			if (!TestNotNull("applied", Applied)) { Effect->RemoveFromRoot(); return; }
			AbilityComp->RemoveActiveAbilityEffect(Applied);
			TestTrue("ended at once", Applied->bCompleted);
			TestEqual("not armed", Applied->EndAtActionTimer, -1.0);
			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
