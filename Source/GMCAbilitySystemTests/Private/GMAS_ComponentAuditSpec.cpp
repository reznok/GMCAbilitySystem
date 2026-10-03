// Component and queue audit specs: the movement pointer at bind, the effect-id allocator, lost
// operations, client-auth payloads on the server, starting effects, the server-operation grace
// setting and the once-only queue state reports. Headless
// UGMAS_TestMovementCmp + UGMC_AbilitySystemComponent harness, no world, no network stack.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Settings/GMASNetworkTimingSettings.h"
#include "Utility/GMASBoundQueueV2_Operations.h"
#include "UGMAS_TestCountingEffect.h"
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

		It("a negative clock wraps into the range and reports once", [this]()
		{
			AddExpectedErrorPlain(TEXT("[EffectID] ServerAuth: ActionTimer"), EAutomationExpectedErrorFlags::Contains, 1);
			AbilityComp->SetActionTimerForTest(-1.0);
			const int First = AbilityComp->GetNextAvailableServerAuthEffectID();
			const int Second = AbilityComp->GetNextAvailableServerAuthEffectID();
			TestTrue("inside the server-auth range", First >= UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset && First < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
			TestTrue("second also inside, no second report", Second >= UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset && Second < UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset);
		});

		It("the bump loop wraps to the range start instead of leaving the range", [this]()
		{
			// Last id of the predicted range is live: the next candidate wraps to 1, not into the
			// server-auth range.
			const int LastPredicted = UGMC_AbilitySystemComponent::ServerAuthEffectIDOffset - 1;
			AbilityComp->SetActionTimerForTest(static_cast<double>(LastPredicted) / 100.0);
			UGMCAbilityEffect* Live = NewObject<UGMCAbilityEffect>(AbilityComp);
			AbilityComp->GetActiveEffectsForTest().Add(LastPredicted, Live);
			const int Id = AbilityComp->GetNextAvailableEffectID();
			TestEqual("wrapped to the first predicted id", Id, 1);
			AbilityComp->GetActiveEffectsForTest().Remove(LastPredicted);
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

		It("a refused activation re-read on the ancillary pass stays silent", [this]()
		{
			AddExpectedErrorPlain(TEXT("No Abilities Granted for InputTag"), EAutomationExpectedErrorFlags::Contains, 2);   // the two refusals
			AbilityComp->bForceAuthorityForTest = true;
			FGMASBoundQueueV2AbilityActivationOperation Op; Op.OperationID = -9; Op.InputTag = ProbeTag;   // no ability granted: refused
			const FInstancedStruct Data = FInstancedStruct::Make(Op);
			AbilityComp->GetBoundQueueV2ForTest().CacheOperationPayload(-9, Data);
			TestFalse("refused on the movement tick", AbilityComp->ProcessOperationForTest(Data, true));
			TestTrue("kept for the ancillary pass", AbilityComp->GetBoundQueueV2ForTest().HasPayloadByID(-9));
			TestFalse("refused on the ancillary pass", AbilityComp->ProcessOperationForTest(Data, false));
			TestFalse("drained", AbilityComp->GetBoundQueueV2ForTest().HasPayloadByID(-9));
			TestFalse("re-read: no [OperationLost]", AbilityComp->ProcessOperationForTest(Data, false));
		});

		It("a client-auth operation leaves no payload on the server and logs no Error", [this]()
		{
			AbilityComp->bForceAuthorityForTest = true;
			AbilityComp->ClientAuthorizedAbilityEffects.Add(UGMCAbilityEffect::StaticClass());
			FGMASBoundQueueV2ClientAuthEffectOperation Op;
			Op.OperationID = -11;
			Op.EffectClass = UGMCAbilityEffect::StaticClass();
			Op.EffectID    = UGMC_AbilitySystemComponent::ClientAuthEffectIDOffset + 300;
			Op.EffectData.EffectType = EGMASEffectType::Persistent;
			Op.EffectData.EffectTag  = ProbeTag;
			const FInstancedStruct Data = FInstancedStruct::Make(Op);
			AbilityComp->ServerProcessOperationForTest(Data, true);
			AbilityComp->ServerProcessOperationForTest(Data, false);   // the ancillary pass re-reads the same slot
			TestEqual("applied once", AbilityComp->GetActiveEffects().Num(), 1);
			TestEqual("no payload left", AbilityComp->GetBoundQueueV2ForTest().GetPayloadCount(), 0);
			AbilityComp->GenAncillaryTick(0.f, false);   // CheckValidState: nothing to report
		});

		It("the invalid-payload report fires once per id", [this]()
		{
			AddExpectedErrorPlain(TEXT("OperationPayloads has a client-made operation id"), EAutomationExpectedErrorFlags::Contains, 2);
			FGMASBoundQueueV2OperationBaseData Base;
			AbilityComp->GetBoundQueueV2ForTest().CacheOperationPayload(-3, FInstancedStruct::Make(Base));
			AbilityComp->GetBoundQueueV2ForTest().CacheOperationPayload(-4, FInstancedStruct::Make(Base));
			AbilityComp->GenAncillaryTick(0.f, false);
			AbilityComp->GenAncillaryTick(0.f, false);
			AbilityComp->GetBoundQueueV2ForTest().RemovePayloadByID(-3);
			AbilityComp->GetBoundQueueV2ForTest().RemovePayloadByID(-4);
		});

		It("starting effects apply on the ancillary tick, never the prediction tick (A#20)", [this]()
		{
			APawn* Pawn = NewObject<APawn>(GetTransientPackage()); Pawn->AddToRoot();
			APlayerController* Controller = NewObject<APlayerController>(GetTransientPackage()); Controller->AddToRoot();
			Pawn->Controller = Controller;
			UGMAS_TestMovementCmp* Move = NewObject<UGMAS_TestMovementCmp>(Pawn);
			UGMC_AbilitySystemComponent* Comp = NewObject<UGMC_AbilitySystemComponent>(Pawn);
			Comp->GMCMovementComponent = Move;
			Comp->BindReplicationData();
			Comp->bForceAuthorityForTest = true;
			Comp->bForceNoAckClientForTest = true;
			Comp->BindServerOpForcedDelegateForTest();

			UGMAS_TestCountingEffect* CDO = GetMutableDefault<UGMAS_TestCountingEffect>();
			const EGMASEffectType SavedType = CDO->EffectData.EffectType;
			CDO->EffectData.EffectType = EGMASEffectType::Persistent;
			Comp->AddStartingEffects({UGMAS_TestCountingEffect::StaticClass()});

			Comp->GenPredictionTick(0.1f);
			TestEqual("the prediction tick applies nothing", Comp->GetActiveEffects().Num(), 0);
			Comp->SetActionTimerForTest(GMASTest::ClientAuthActionTimer);   // GenPredictionTick wrote the stub's -1
			Comp->GenAncillaryTick(0.1f, false);   // queued with zero grace
			Comp->GenAncillaryTick(0.1f, false);   // forced
			TestEqual("applied once", Comp->GetActiveEffects().Num(), 1);
			Comp->GenAncillaryTick(0.1f, false);
			TestEqual("still once", Comp->GetActiveEffects().Num(), 1);

			CDO->EffectData.EffectType = SavedType;
			Controller->RemoveFromRoot();
			Pawn->RemoveFromRoot();
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
