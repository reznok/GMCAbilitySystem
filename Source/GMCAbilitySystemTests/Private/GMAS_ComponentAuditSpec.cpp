// Component and queue audit specs: the movement pointer at bind, the effect-id allocator, lost
// operations, the server-operation grace setting and the once-only queue state reports. Headless
// UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent harness, no world, no network stack.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "GameFramework/Actor.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "Utility/GMASBoundQueueV2_Operations.h"
#include "UGMAS_TestMovementCmp.h"
#include "GMAS_TestHelpers.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASComponentAuditSpec,
	"GMAS.Unit.ComponentAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*          MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent*    AbilityComp = nullptr;
	UGMCAttributesData*             AttrData    = nullptr;

	FGameplayTag HealthTag;
	FGameplayTag ProbeTag;

	void SetupHarness();
	void TeardownHarness();

END_DEFINE_SPEC(FGMASComponentAuditSpec)

void FGMASComponentAuditSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.CompAudit.Attribute.Health"), TEXT("Health for component audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SProbeTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.CompAudit.Probe"), TEXT("Probe tag for component audit tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag = SHealthTag.GetTag();
	ProbeTag  = SProbeTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData D;
	D.AttributeTag = HealthTag;
	D.DefaultValue = 100.f;
	D.Clamp.Min    = 0.f;
	D.Clamp.Max    = 200.f;
	D.bGMCBound    = true;
	AttrData->AttributeData.Add(D);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->SetActionTimerForTest(GMASTest::ClientAuthActionTimer);   // positive: ids land inside their ranges
}

void FGMASComponentAuditSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

void FGMASComponentAuditSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("Bind", [this]()
	{
		It("resolves a null movement pointer from the owner", [this]()
		{
			AActor* Owner = NewObject<AActor>(GetTransientPackage()); Owner->AddToRoot();
			UGMAS_TestMovementCmp* Move = NewObject<UGMAS_TestMovementCmp>(Owner); Owner->AddOwnedComponent(Move);
			UGMC_AbilitySystemComponent* Comp = NewObject<UGMC_AbilitySystemComponent>(Owner);
			Comp->BindReplicationData();
			TestTrue("resolved", Comp->GMCMovementComponent == static_cast<UGMC_MovementUtilityCmp*>(Move));
			Owner->RemoveFromRoot();
		});

		It("logs and binds nothing when there is no movement component", [this]()
		{
			AddExpectedErrorPlain(TEXT("no GMCMovementComponent"), EAutomationExpectedErrorFlags::Contains, 1);
			UGMC_AbilitySystemComponent* Comp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage()); Comp->AddToRoot();
			Comp->AttributeDataAssets.Add(AttrData);
			Comp->BindReplicationData();
			TestEqual("no bound attributes", Comp->BoundAttributes.Attributes.Num(), 0);
			Comp->RemoveFromRoot();
		});
	});

	Describe("Effect ids", [this]()
	{
		It("wraps within the range past its end and reports once", [this]()
		{
			AddExpectedErrorPlain(TEXT("[EffectID] Predicted: ActionTimer"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->SetActionTimerForTest(static_cast<double>(UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset) / 100.0 + 5.0);
			const int First = AbilityComp->GetNextAvailableEffectID();
			const int Second = AbilityComp->GetNextAvailableEffectID();
			TestTrue("inside the predicted range", First > 0 && First < UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset);
			TestTrue("second also inside, no second report", Second > 0 && Second < UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset);
		});

		It("ActionTimer 0 refuses the apply with one Error and registers nothing", [this]()
		{
			AddExpectedErrorPlain(TEXT("ActionTimer is 0, cannot generate"), EAutomationExpectedErrorFlags::Contains, 1);
			AddExpectedErrorPlain(TEXT("no id for"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->SetActionTimerForTest(0.0);
			int Handle = -1, Id = -1; UGMCAbilityEffect* Out = nullptr;
			FGMCAbilityEffectData Data; Data.EffectType = EGMASEffectType::Persistent; Data.EffectTag = ProbeTag;
			TestFalse("refused", AbilityComp->ApplyAbilityEffect(UGMCAbilityEffect::StaticClass(), Data, EGMCAbilityEffectQueueType::Predicted, Handle, Id, Out));
			TestEqual("nothing registered", AbilityComp->GetActiveEffects().Num(), 0);
		});
	});

	Describe("Operations", [this]()
	{
		It("a lost client operation is reported once on the server", [this]()
		{
			AddExpectedErrorPlain(TEXT("[OperationLost]"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->bForceAuthorityForTest = true;
			FGMASBoundQueueV2AbilityActivationOperation Op; Op.OperationID = -7; Op.InputTag = ProbeTag;
			const FInstancedStruct Data = FInstancedStruct::Make(Op);
			TestFalse("not applied", AbilityComp->ProcessOperationForTest(Data, true));
			TestFalse("still not applied, silent", AbilityComp->ProcessOperationForTest(Data, true));
		});

		It("the server grace comes from the settings", [this]()
		{
			UGMASNetworkTimingSettings* Settings = GetMutableDefault<UGMASNetworkTimingSettings>();
			const float SavedGrace = Settings->ServerOperationGraceSeconds;
			Settings->ServerOperationGraceSeconds = 2.5f;
			AbilityComp->bForceAuthorityForTest = true;
			FGMASBoundQueueV2AddImpulseOperation Impulse; Impulse.Impulse = FVector(1, 0, 0);
			const int OpID = AbilityComp->GetBoundQueueV2ForTest().MakeOperationData<FGMASBoundQueueV2AddImpulseOperation>(Impulse);
			AbilityComp->EnqueueServerOperationForTest(OpID);
			TestEqual("grace 2.5", AbilityComp->GetBoundQueueV2ForTest().ServerQueuedBoundOperationsGracePeriods.FindRef(OpID), 2.5f);
			Settings->ServerOperationGraceSeconds = SavedGrace;
		});

		It("CheckValidState reports a stale client queue on the server once", [this]()
		{
			AddExpectedErrorPlain(TEXT("ClientQueuedOperations has"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations.Add(5);
			AbilityComp->GenAncillaryTick(0.f, false);
			AbilityComp->GenAncillaryTick(0.f, false);
			AbilityComp->GetBoundQueueV2ForTest().ClientQueuedOperations.Reset();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
