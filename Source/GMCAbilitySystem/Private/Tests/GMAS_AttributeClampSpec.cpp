// Tests for FAttributeClamp::IsSet and FAttributeClamp::ClampValue.
// These cover the no-UObject paths: clamp unset passthrough, and
// static min/max bounds when AbilityComponent is nullptr.

#include "Misc/AutomationTest.h"
#include "Attributes/GMCAttributeClamp.h"

#if WITH_AUTOMATION_WORKER

BEGIN_DEFINE_SPEC(FGMASAttributeClampSpec,
	"GMAS.Unit.AttributeClamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
END_DEFINE_SPEC(FGMASAttributeClampSpec)

void FGMASAttributeClampSpec::Define()
{
	Describe("IsSet", [this]()
	{
		It("returns false when Min, Max, and all tags are default", [this]()
		{
			FAttributeClamp Clamp;
			TestFalse("default clamp is unset", Clamp.IsSet());
		});

		It("returns true when Min is non-zero", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 1.f;
			TestTrue("Min=1 is set", Clamp.IsSet());
		});

		It("returns true when Max is non-zero", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Max = 100.f;
			TestTrue("Max=100 is set", Clamp.IsSet());
		});

		It("returns false when Min=0 and Max=0 — cannot clamp to [0,0] range", [this]()
		{
			// The zero state is explicitly the 'unset' sentinel. A [0,0] range
			// cannot be expressed — use Min=0, Max=epsilon or a MaxAttributeTag instead.
			FAttributeClamp Clamp;
			Clamp.Min = 0.f;
			Clamp.Max = 0.f;
			TestFalse("zero range is treated as unset", Clamp.IsSet());
		});
	});

	Describe("ClampValue with no AbilityComponent", [this]()
	{
		It("passes through any value when clamp is unset", [this]()
		{
			FAttributeClamp Clamp; // Min=0, Max=0, no tags
			TestEqual("positive passthrough", Clamp.ClampValue(250.f), 250.f);
			TestEqual("negative passthrough", Clamp.ClampValue(-999.f), -999.f);
			TestEqual("zero passthrough", Clamp.ClampValue(0.f), 0.f);
		});

		It("returns value unchanged when it is within [Min, Max]", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 0.f;
			Clamp.Max = 100.f;
			TestEqual("midrange value", Clamp.ClampValue(50.f), 50.f);
			TestEqual("at min boundary", Clamp.ClampValue(0.f), 0.f);
			TestEqual("at max boundary", Clamp.ClampValue(100.f), 100.f);
		});

		It("clamps below-Min value up to Min", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 10.f;
			Clamp.Max = 90.f;
			TestEqual("below min clamped to 10", Clamp.ClampValue(5.f), 10.f);
			TestEqual("large negative clamped to 10", Clamp.ClampValue(-9999.f), 10.f);
		});

		It("clamps above-Max value down to Max", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 0.f;
			Clamp.Max = 200.f;
			TestEqual("above max clamped to 200", Clamp.ClampValue(201.f), 200.f);
			TestEqual("large positive clamped to 200", Clamp.ClampValue(9999.f), 200.f);
		});

		It("clamps fractional values correctly", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 0.5f;
			Clamp.Max = 99.5f;
			TestEqual("fractional value in range", Clamp.ClampValue(50.25f), 50.25f);
			TestEqual("fractional below min", Clamp.ClampValue(0.1f), 0.5f);
			TestEqual("fractional above max", Clamp.ClampValue(99.9f), 99.5f);
		});

		It("clamps with negative Min (e.g. debuff floor)", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = -50.f;
			Clamp.Max = 50.f;
			TestEqual("in range", Clamp.ClampValue(0.f), 0.f);
			TestEqual("below negative min", Clamp.ClampValue(-100.f), -50.f);
			TestEqual("above positive max", Clamp.ClampValue(100.f), 50.f);
		});
	});
}

#endif // WITH_AUTOMATION_WORKER
