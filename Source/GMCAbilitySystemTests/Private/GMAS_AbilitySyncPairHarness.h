// Pair harness for the ability-sync specs: one client and one server component (orphan components on
// UGMAS_TestMovementCmp stubs, the server forced to authority, both forced to have a remote twin). Every
// ability-sync message either side sends lands in one outbox instead of an RPC; the spec delivers, drops
// or reorders it. Activations go through the real operation paths with a hand-built negative (client)
// operation id, as the client and server would each process the same activation operation.

#pragma once

#include "CoreMinimal.h"
#include "Algo/Reverse.h"
#include "NativeGameplayTags.h"
#include "Ability/GMCAbility.h"
#include "Components/GMCAbilityComponent.h"
#include "Utility/GMASAbilitySyncRules.h"
#include "Utility/GMASBoundQueueV2_Operations.h"
#include "UGMAS_TestMovementCmp.h"
#include "GMAS_TestHelpers.h"

struct FGMASAbilitySyncPairHarness
{
	UGMAS_TestMovementCmp* ClientMoveCmp = nullptr;
	UGMAS_TestMovementCmp* ServerMoveCmp = nullptr;
	UGMC_AbilitySystemComponent* Client = nullptr;
	UGMC_AbilitySystemComponent* Server = nullptr;

	// Messages sent and not yet delivered, oldest first. Key: true = client -> server.
	TArray<TPair<bool, FGMASAbilitySyncMessage>> Outbox;

	static FGameplayTag InputTag()
	{
		static FNativeGameplayTag SInputTag(
			TEXT("GMCAbilitySystem"), TEXT("GMCAbilitySystem"),
			TEXT("GMAS.Test.AbilitySync.Input"), TEXT("Input tag for the ability-sync pair specs"),
			ENativeGameplayTagToken::PRIVATE_USE_MACRO_INSTEAD);
		return SInputTag.GetTag();
	}

	// Builds both sides and grants the same candidates (in order) for InputTag() on each.
	void Setup(const TArray<TSubclassOf<UGMCAbility>>& Candidates)
	{
		Client = MakeSide(ClientMoveCmp, /*bAuthority=*/false);
		Server = MakeSide(ServerMoveCmp, /*bAuthority=*/true);
		for (UGMC_AbilitySystemComponent* Side : { Client, Server })
		{
			FAbilityMapData MapData;
			MapData.InputTag = InputTag();
			MapData.Abilities = Candidates;
			Side->AddAbilityMapData(MapData);
			Side->GrantAbilityByTag(InputTag());
		}
		Client->SyncSendHookForTest = [this](const FGMASAbilitySyncMessage& Message, bool bToServer)
		{
			Outbox.Add({ bToServer, Message });
		};
		Server->SyncSendHookForTest = [this](const FGMASAbilitySyncMessage& Message, bool bToServer)
		{
			Outbox.Add({ bToServer, Message });
		};
	}

	void Teardown()
	{
		for (UGMC_AbilitySystemComponent* Side : { Client, Server })
		{
			if (Side)
			{
				Side->SyncSendHookForTest = nullptr;
				Side->RemoveFromRoot();
			}
		}
		for (UGMAS_TestMovementCmp* MoveCmp : { ClientMoveCmp, ServerMoveCmp })
		{
			if (MoveCmp)
			{
				MoveCmp->RemoveFromRoot();
			}
		}
		Client = Server = nullptr;
		ClientMoveCmp = ServerMoveCmp = nullptr;
		Outbox.Reset();
	}

	// Both sides process the same client activation operation (OperationID < 0, as a client stamps it).
	void ActivateBothFromOperation(const FGameplayTag& Tag, int32 OperationID, bool bFromMovementTick = true)
	{
		ActivateClientFromOperation(Tag, OperationID, bFromMovementTick);
		ActivateServerFromOperation(Tag, OperationID, bFromMovementTick);
	}

	void ActivateClientFromOperation(const FGameplayTag& Tag, int32 OperationID, bool bFromMovementTick = true)
	{
		const FInstancedStruct Data = MakeActivation(Tag, OperationID);
		// The client caches its own operation's payload when it queues it.
		Client->GetBoundQueueV2ForTest().CacheOperationPayload(OperationID, Data);
		Client->ProcessOperationForTest(Data, bFromMovementTick);
	}

	void ActivateServerFromOperation(const FGameplayTag& Tag, int32 OperationID, bool bFromMovementTick = true)
	{
		Server->ServerProcessOperationForTest(MakeActivation(Tag, OperationID), bFromMovementTick);
	}

	// Delivers every queued message in order, including messages sent while delivering.
	int32 DeliverAll()
	{
		int32 Delivered = 0;
		while (Outbox.Num() > 0)
		{
			const TPair<bool, FGMASAbilitySyncMessage> Entry = Outbox[0];
			Outbox.RemoveAt(0);
			(Entry.Key ? Server : Client)->ReceiveAbilitySyncForTest(Entry.Value);
			++Delivered;
		}
		return Delivered;
	}

	// Drops the oldest queued message of Type; returns whether one was dropped.
	bool DropNext(EGMASAbilitySyncType Type)
	{
		const int32 Index = Outbox.IndexOfByPredicate([Type](const TPair<bool, FGMASAbilitySyncMessage>& Entry)
		{
			return Entry.Value.Type == Type;
		});
		if (Index == INDEX_NONE)
		{
			return false;
		}
		Outbox.RemoveAt(Index);
		return true;
	}

	// Reverses the delivery order of the queued messages.
	void Reorder()
	{
		Algo::Reverse(Outbox);
	}

	int32 CountOutbox(EGMASAbilitySyncType Type) const
	{
		return Outbox.FilterByPredicate([Type](const TPair<bool, FGMASAbilitySyncMessage>& Entry)
		{
			return Entry.Value.Type == Type;
		}).Num();
	}

	void AdvanceClocks(double Seconds)
	{
		Client->AdvanceConfirmClockForTest(Seconds);
		Server->AdvanceConfirmClockForTest(Seconds);
	}

	// Ticks every active ability on both sides and purges ended instances.
	void Tick(float DeltaTime = 1.f / 30.f)
	{
		for (UGMC_AbilitySystemComponent* Side : { Client, Server })
		{
			Side->TickActiveAbilitiesForTest(DeltaTime);
			Side->CleanupStaleAbilitiesForTest();
		}
	}

	static UGMCAbility* FindByID(UGMC_AbilitySystemComponent* Side, int32 AbilityID)
	{
		const TMap<int, UGMCAbility*> Active = Side->GetActiveAbilities();   // returned by value
		UGMCAbility* const* Found = Active.Find(AbilityID);
		return Found ? *Found : nullptr;
	}

private:
	static UGMC_AbilitySystemComponent* MakeSide(UGMAS_TestMovementCmp*& OutMoveCmp, bool bAuthority)
	{
		OutMoveCmp = NewObject<UGMAS_TestMovementCmp>(GetTransientPackage());
		OutMoveCmp->AddToRoot();
		UGMC_AbilitySystemComponent* Side = NewObject<UGMC_AbilitySystemComponent>(GetTransientPackage());
		Side->AddToRoot();
		Side->GMCMovementComponent = OutMoveCmp;
		Side->BindReplicationData();
		Side->SetActionTimerForTest(GMASTest::StableActionTimer);
		Side->SilenceEffectIDWrapReportForTest();
		Side->bForceAuthorityForTest = bAuthority;
		Side->bForceRemoteTwinForTest = true;
		return Side;
	}

	static FInstancedStruct MakeActivation(const FGameplayTag& Tag, int32 OperationID)
	{
		FGMASBoundQueueV2AbilityActivationOperation Op;
		Op.OperationID = OperationID;
		Op.InputTag = Tag;
		return FInstancedStruct::Make(Op);
	}
};
