#pragma once
#include "StateTreeGMASTask.h"
#include "Components/StateTreeComponent.h"
#include "StateTreeGMASLogTask.generated.h"

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASLogInstanceData
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Write Log Message", Category = "GMAS|Log"))
struct GMCABILITYSYSTEMAI_API FStateTreeGMASLogTask : public FStateTreeGMASTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASLogInstanceData;

	FStateTreeGMASLogTask() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString LogMessage;
};
