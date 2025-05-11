#pragma once

#include "StateTreeConditionBase.h"
#include "StateTreeGMASConditionBase.generated.h"

// Base class of all AI condition that expect to be run on an AIController or derived class 
USTRUCT(meta = (Hidden, Category = "GMAS"))
struct GMCABILITYSYSTEMAI_API FStateTreeGMASConditionBase : public FStateTreeConditionBase
{
	GENERATED_BODY()
};