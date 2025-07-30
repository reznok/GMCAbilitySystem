#pragma once
#include "StateTreeGMASTask.h"
#include "Components/StateTreeComponent.h"
#include "LogTask.generated.h"

USTRUCT()
struct GMCABILITYSYSTEMAI_API FGMASLogInstanceData
{
	GENERATED_BODY()
};

USTRUCT(meta = (DisplayName = "Write Log Message", Category = "GMAS"))
struct GMCABILITYSYSTEMAI_API FLogTask : public FStateTreeGMASTaskBase
{
	GENERATED_BODY()

	using FInstanceDataType = FGMASLogInstanceData;

	FLogTask() = default;

	virtual const UStruct* GetInstanceDataType() const override { return FInstanceDataType::StaticStruct(); }

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) const override;

	UPROPERTY(EditAnywhere, Category = "Parameter")
	FString LogMessage;
};
