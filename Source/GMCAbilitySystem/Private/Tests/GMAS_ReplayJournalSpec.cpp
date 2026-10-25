// Replay journal: a client replay rewinds bound tags and attributes, but not the effects, and it skips consumed
// activation operations. These tests run live moves, rewind the bound state as GMC does, then replay the moves.
// Move times stay below the client confirmation timeout (0.5 s) so no predicted effect is reaped.

#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Attributes/GMCAttributesData.h"
#include "Components/GMCAbilityComponent.h"
#include "Effects/GMCAbilityEffect.h"
#include "Attributes/GMCAttributeModifier.h"
#include "UGMAS_TestMovementCmp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASReplayJournalSpec,
	"GMAS.Unit.ReplayJournal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	UGMAS_TestMovementCmp*       MoveCmp     = nullptr;
	UGMC_AbilitySystemComponent* AbilityComp = nullptr;
	UGMCAttributesData*          AttrData    = nullptr;
	FGameplayTag HealthTag;
	FGameplayTag BurningTag;

	void SetupHarness();
	void TeardownHarness();
	void TickMove(double MoveTime, bool bReplay);
	void RewindBoundState(float Value, float RawValue, bool bHasBurningTag);
	FGMCAbilityEffectData MakeData(EGMASEffectType Type, float Amount) const;

END_DEFINE_SPEC(FGMASReplayJournalSpec)

void FGMASReplayJournalSpec::SetupHarness()
{
	static FNativeGameplayTag SHealthTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Attribute.Health"), TEXT("Health for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	static FNativeGameplayTag SBurningTag(
		TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
		TEXT("GMAS.Test.Status.Burning"), TEXT("Burning for GMAS tests"),
		ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
	HealthTag  = SHealthTag.GetTag();
	BurningTag = SBurningTag.GetTag();

	MoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
	MoveCmp->AddToRoot();

	AbilityComp = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
	AbilityComp->AddToRoot();

	AttrData = NewObject<UGMCAttributesData>(GetTransientPackage());
	AttrData->AddToRoot();

	FAttributeData HealthData;
	HealthData.AttributeTag = HealthTag;
	HealthData.DefaultValue = 100.f;
	HealthData.Clamp.Min    = 0.f;
	HealthData.Clamp.Max    = 200.f;
	HealthData.bGMCBound    = true;
	AttrData->AttributeData.Add(HealthData);

	AbilityComp->AttributeDataAssets.Add(AttrData);
	AbilityComp->GMCMovementComponent = MoveCmp;
	AbilityComp->BindReplicationData();
	AbilityComp->ActionTimer = -1.0;

	// The harness has no locally controlled pawn and no move timestamps.
	AbilityComp->bForceReplayJournalForTest = true;
	AbilityComp->bUseMoveTimestampOverrideForTest = true;
}

void FGMASReplayJournalSpec::TeardownHarness()
{
	AttrData->RemoveFromRoot();
	AbilityComp->RemoveFromRoot();
	MoveCmp->RemoveFromRoot();
	AttrData = nullptr; AbilityComp = nullptr; MoveCmp = nullptr;
}

void FGMASReplayJournalSpec::TickMove(double MoveTime, bool bReplay)
{
	AbilityComp->MoveTimestampOverrideForTest = MoveTime;
	AbilityComp->bForceReplayingForTest = bReplay;
	AbilityComp->GenPredictionTick(1.f);
	AbilityComp->bForceReplayingForTest = false;
}

void FGMASReplayJournalSpec::RewindBoundState(float Value, float RawValue, bool bHasBurningTag)
{
	// GMC writes the server source state into the bound variables before it replays.
	if (const FAttribute* Health = AbilityComp->GetAttributeByTag(HealthTag))
	{
		Health->Value = Value;
		Health->RawValue = RawValue;
	}
	if (bHasBurningTag)
	{
		AbilityComp->AddActiveTag(BurningTag);
	}
	else
	{
		AbilityComp->RemoveActiveTag(BurningTag);
	}
}

FGMCAbilityEffectData FGMASReplayJournalSpec::MakeData(EGMASEffectType Type, float Amount) const
{
	FGMCAbilityEffectData Data;
	Data.EffectType         = Type;
	Data.bNegateEffectAtEnd = Type == EGMASEffectType::Persistent;
	Data.Duration           = 0.f;
	Data.EffectTag          = BurningTag;
	Data.GrantedTags.AddTag(BurningTag);

	FGMCAttributeModifier Mod;
	Mod.AttributeTag  = HealthTag;
	Mod.Op            = EModifierType::Add;
	Mod.ValueType     = EGMCAttributeModifierType::AMT_Value;
	Mod.ModifierValue = Amount;
	Data.Modifiers.Add(Mod);
	return Data;
}

void FGMASReplayJournalSpec::Define()
{
	BeforeEach([this]() { SetupHarness();    });
	AfterEach ([this]() { TeardownHarness(); });

	Describe("Effect started inside the replayed window", [this]()
	{
		It("gets its tag and temporal modifier back", [this]()
		{
			TickMove(1.00, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Persistent, 30.f));
			TickMove(1.01, false);
			TickMove(1.02, false);
			TestEqual("Live: Value buffed to 130", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
			TestTrue("Live: tag granted", AbilityComp->HasActiveTag(BurningTag));

			// Server source = move 1.00, before the effect.
			RewindBoundState(100.f, 100.f, false);
			TickMove(1.01, true);
			TickMove(1.02, true);
			TestEqual("Replay: Value buffed to 130 again", AbilityComp->GetAttributeValueByTag(HealthTag), 130.f);
			TestTrue("Replay: tag granted again", AbilityComp->HasActiveTag(BurningTag));

			Effect->RemoveFromRoot();
		});
	});

	Describe("Effect ended inside the replayed window", [this]()
	{
		It("loses its tag and modifier at its recorded end, then its ghost is pruned", [this]()
		{
			TickMove(2.00, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Persistent, 30.f));
			TickMove(2.01, false);
			TickMove(2.02, false);
			AbilityComp->RemoveActiveAbilityEffect(Effect);
			TickMove(2.03, false);
			TickMove(2.04, false);
			TestEqual("Live: Value back to 100", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestFalse("Live: tag removed", AbilityComp->HasActiveTag(BurningTag));
			TestEqual("Live: one ghost kept", AbilityComp->GetReplayGhostCountForTest(), 1);

			// Server source = move 2.02, while the effect was still active.
			RewindBoundState(130.f, 100.f, true);
			TickMove(2.03, true);
			TestEqual("Replay: Value back to 100 at the recorded end", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			TestFalse("Replay: tag removed at the recorded end", AbilityComp->HasActiveTag(BurningTag));
			TickMove(2.04, true);

			// The server acknowledged the end: no replay reaches it any more.
			AbilityComp->SetLastReceivedAPMoveTimestampForTest(2.03);
			TickMove(2.05, false);
			TestEqual("Ghost pruned", AbilityComp->GetReplayGhostCountForTest(), 0);

			Effect->RemoveFromRoot();
		});
	});

	Describe("Ticking effect started inside the replayed window", [this]()
	{
		It("does not tick on replayed moves before its start", [this]()
		{
			TickMove(3.00, false);
			TickMove(3.01, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Ticking, -10.f));
			TickMove(3.02, false);
			TickMove(3.03, false);
			const float LiveRaw = AbilityComp->GetAttributeRawValue(HealthTag);

			// Server source = move 3.00: two moves before the effect start.
			RewindBoundState(100.f, 100.f, false);
			TickMove(3.01, true);
			TickMove(3.02, true);
			TickMove(3.03, true);
			TestEqual("Replay drains as much as the live run", AbilityComp->GetAttributeRawValue(HealthTag), LiveRaw);

			Effect->RemoveFromRoot();
		});
	});

	Describe("Bound effect ID list", [this]()
	{
		It("stays sorted whatever the add order", [this]()
		{
			AbilityComp->BoundActiveEffectIDs_Add(50);
			AbilityComp->BoundActiveEffectIDs_Add(10);
			AbilityComp->BoundActiveEffectIDs_Add(30);
			AbilityComp->BoundActiveEffectIDs_Add(10);
			TestTrue("Sorted, no duplicate", AbilityComp->GetBoundActiveEffectIDsForTest() == TArray<int>{10, 30, 50});
		});

		It("is rebuilt by the replay as the live run wrote it", [this]()
		{
			TickMove(4.00, false);
			UGMCAbilityEffect* Ended = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Ended->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Ended, MakeData(EGMASEffectType::Persistent, 30.f));
			const int EndedId = Ended->EffectData.EffectID;
			TickMove(4.01, false);
			const TArray<int> SourceIDs = AbilityComp->GetBoundActiveEffectIDsForTest();

			UGMCAbilityEffect* Started = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Started->AddToRoot();
			FGMCAbilityEffectData BareData = MakeData(EGMASEffectType::Persistent, 0.f);
			BareData.EffectTag = FGameplayTag();
			BareData.GrantedTags.Reset();
			BareData.Modifiers.Reset();
			AbilityComp->ApplyAbilityEffect(Started, BareData);
			const int StartedId = Started->EffectData.EffectID;
			TickMove(4.02, false);
			AbilityComp->RemoveActiveAbilityEffect(Ended);
			TickMove(4.03, false);
			const TArray<int> LiveIDs = AbilityComp->GetBoundActiveEffectIDsForTest();
			TestTrue("Live: started ID added", LiveIDs.Contains(StartedId));
			TestFalse("Live: ended ID cleaned", LiveIDs.Contains(EndedId));

			// Server source = move 4.01: Ended is active, Started does not exist yet.
			AbilityComp->SetBoundActiveEffectIDsForTest(SourceIDs);
			RewindBoundState(130.f, 100.f, true);
			TickMove(4.02, true);
			TestTrue("Replay at 4.02: both IDs, sorted", AbilityComp->GetBoundActiveEffectIDsForTest() == TArray<int>{EndedId, StartedId});
			TickMove(4.03, true);
			TestTrue("Replay: same list as the live run", AbilityComp->GetBoundActiveEffectIDsForTest() == LiveIDs);

			Started->RemoveFromRoot();
			Ended->RemoveFromRoot();
		});

		It("keeps the ID of an effect that ended after the last move until the next local move", [this]()
		{
			TickMove(5.00, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Persistent, 30.f));
			const int Id = Effect->EffectData.EffectID;
			TickMove(5.01, false);
			TickMove(5.02, false);
			// Ended outside a prediction tick: the next local move cleans it.
			AbilityComp->RemoveActiveAbilityEffect(Effect);

			// Server source = move 5.01. The replay runs before the next local move.
			AbilityComp->SetBoundActiveEffectIDsForTest({Id});
			RewindBoundState(130.f, 100.f, true);
			TickMove(5.02, true);
			AbilityComp->CL_OnPostReplay();
			TestTrue("Replay: ID kept on the replayed move", AbilityComp->BoundActiveEffectIDs_Contains(Id));
			TestFalse("Replay: tag removed after the replay", AbilityComp->HasActiveTag(BurningTag));

			TickMove(5.03, false);
			TestFalse("Next local move: ID cleaned", AbilityComp->BoundActiveEffectIDs_Contains(Id));
			TestEqual("Next local move: Value back to 100", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);

			Effect->RemoveFromRoot();
		});

		It("ends at its start an effect that the server ended before it started here", [this]()
		{
			TickMove(6.00, false);
			TickMove(6.01, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Persistent, 30.f));
			const int Id = Effect->EffectData.EffectID;
			TickMove(6.02, false);
			TestTrue("Live: ID added at 6.02", AbilityComp->BoundActiveEffectIDs_Contains(Id));
			AbilityComp->RPCClientEndEffectForTest(Id, 6.00);

			// Server source = move 6.00: the server already ended the effect there.
			AbilityComp->SetLastReceivedAPMoveTimestampForTest(6.00);
			AbilityComp->SetBoundActiveEffectIDsForTest({});
			RewindBoundState(100.f, 100.f, false);
			AbilityComp->CL_OnPreReplay();
			TickMove(6.01, true);
			TickMove(6.02, true);
			TestFalse("Replay 6.02: no ID", AbilityComp->BoundActiveEffectIDs_Contains(Id));
			TestFalse("Replay 6.02: no tag", AbilityComp->HasActiveTag(BurningTag));
			TestEqual("Replay 6.02: Value 100", AbilityComp->GetAttributeValueByTag(HealthTag), 100.f);
			AbilityComp->CL_OnPostReplay();

			TickMove(6.03, false);
			TestFalse("Next local move: no ID", AbilityComp->BoundActiveEffectIDs_Contains(Id));
			TestFalse("Next local move: no tag", AbilityComp->HasActiveTag(BurningTag));

			Effect->RemoveFromRoot();
		});

		It("re-runs a write made after the last move once per replay, and never twice", [this]()
		{
			TickMove(8.00, false);
			TickMove(8.01, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			FGMCAbilityEffectData BareData = MakeData(EGMASEffectType::Persistent, 0.f);
			BareData.EffectTag = FGameplayTag();
			BareData.GrantedTags.Reset();
			BareData.Modifiers.Reset();
			AbilityComp->ApplyAbilityEffect(Effect, BareData);
			const int Id = Effect->EffectData.EffectID;

			// Two replays from move 8.00 before the next local move. Each rewind drops the late Add.
			for (int32 Pass = 0; Pass < 2; ++Pass)
			{
				AbilityComp->SetLastReceivedAPMoveTimestampForTest(8.00);
				AbilityComp->SetBoundActiveEffectIDsForTest({});
				AbilityComp->CL_OnPreReplay();
				TickMove(8.01, true);
				TestFalse("Replayed move 8.01: no ID yet", AbilityComp->BoundActiveEffectIDs_Contains(Id));
				AbilityComp->CL_OnPostReplay();
				TestTrue("After the replay: late Add put back", AbilityComp->GetBoundActiveEffectIDsForTest() == TArray<int>{Id});
			}

			TickMove(8.02, false);
			TestTrue("Next local move: ID once", AbilityComp->GetBoundActiveEffectIDsForTest() == TArray<int>{Id});

			Effect->RemoveFromRoot();
		});

		It("drops the writes once the server acknowledged their move", [this]()
		{
			TickMove(7.00, false);
			UGMCAbilityEffect* Effect = NewObject<UGMCAbilityEffect>(GetTransientPackage());
			Effect->AddToRoot();
			AbilityComp->ApplyAbilityEffect(Effect, MakeData(EGMASEffectType::Persistent, 30.f));
			TickMove(7.01, false);
			TestEqual("One write kept", AbilityComp->GetReplayBoundEffectIDWriteCountForTest(), 1);

			AbilityComp->SetLastReceivedAPMoveTimestampForTest(7.01);
			TickMove(7.02, false);
			TestEqual("Write pruned", AbilityComp->GetReplayBoundEffectIDWriteCountForTest(), 0);

			Effect->RemoveFromRoot();
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
