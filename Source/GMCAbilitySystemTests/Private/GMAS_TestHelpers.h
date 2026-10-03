// Shared helpers for the GMAS specs: attribute builders and the clock seeds the component
// harnesses use.
#pragma once

#include "CoreMinimal.h"
#include "Attributes/GMCAttributes.h"

namespace GMASTest
{
	// The stub movement component never executes a move, so GetMoveTimestamp() returns its
	// default -1.0 and GenPredictionTick writes that into ActionTimer. Seeding -1.0 keeps the
	// clock stable across GenPredictionTick calls. A negative clock is outside every effect-id
	// range, so each generator wraps it into its own range and reports that once per component
	// (an Error; harnesses on this clock call SilenceEffectIDWrapReportForTest). Only 0 is
	// refused by the effect-id generators.
	inline constexpr double StableActionTimer = -1.0;

	// Client-auth ids are ActionTimer*100 + ClientAuthEffectIDOffset and must not drop below the
	// offset, so client-auth specs seed a positive clock.
	inline constexpr double ClientAuthActionTimer = 1.0;

	// An attribute with no clamp. FAttributeClamp defaults to both flags on with Min = Max = 0,
	// which pins the value at 0: the cause of the standalone attribute specs failing before 1.4.1.
	inline FAttribute MakeAttr(float InitialValue)
	{
		FAttribute Attr;
		Attr.InitialValue = InitialValue;
		Attr.Clamp.bClampMin = false;
		Attr.Clamp.bClampMax = false;
		Attr.Init();
		return Attr;
	}

	// An attribute clamped to [Min, Max]; a flag set to false leaves that side open.
	inline FAttribute MakeClampedAttr(float InitialValue, float Min, float Max,
		bool bClampMin = true, bool bClampMax = true)
	{
		FAttribute Attr;
		Attr.InitialValue = InitialValue;
		Attr.Clamp.bClampMin = bClampMin;
		Attr.Clamp.Min = Min;
		Attr.Clamp.bClampMax = bClampMax;
		Attr.Clamp.Max = Max;
		Attr.Init();
		return Attr;
	}
}
