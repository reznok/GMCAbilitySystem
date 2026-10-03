// Tests for FAttributeClamp::IsSet and FAttributeClamp::ClampValue.
// Covers the explicit bClampMin/bClampMax flags: each bound is applied
// independently, and the old [0,0] "unset" sentinel no longer exists.

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
		It("returns true by default — both clamp flags default to true", [this]()
		{
			FAttributeClamp Clamp;
			TestTrue("default clamp clamps both ends", Clamp.IsSet());
		});

		It("returns false when both clamp flags are disabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = false;
			Clamp.bClampMax = false;
			TestFalse("no clamp when both flags off", Clamp.IsSet());
		});

		It("returns true when only bClampMin is enabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = true;
			Clamp.bClampMax = false;
			TestTrue("min-only clamp is set", Clamp.IsSet());
		});

		It("returns true when only bClampMax is enabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = false;
			Clamp.bClampMax = true;
			TestTrue("max-only clamp is set", Clamp.IsSet());
		});
	});

	Describe("ClampValue with no AbilityComponent", [this]()
	{
		It("passes through any value when both flags are disabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = false;
			Clamp.bClampMax = false;
			TestEqual("positive passthrough", Clamp.ClampValue(250.f), 250.f);
			TestEqual("negative passthrough", Clamp.ClampValue(-999.f), -999.f);
			TestEqual("zero passthrough", Clamp.ClampValue(0.f), 0.f);
		});

		It("clamps both ends when both flags are enabled", [this]()
		{
			FAttributeClamp Clamp; // bClampMin / bClampMax default true
			Clamp.Min = 0.f;
			Clamp.Max = 100.f;
			TestEqual("midrange value", Clamp.ClampValue(50.f), 50.f);
			TestEqual("below min clamped to 0", Clamp.ClampValue(-10.f), 0.f);
			TestEqual("above max clamped to 100", Clamp.ClampValue(150.f), 100.f);
		});

		It("clamps only the lower bound when bClampMax is disabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = true;
			Clamp.bClampMax = false;
			Clamp.Min = 0.f;
			Clamp.Max = 100.f; // Max is ignored — flag is off
			TestEqual("below min clamped to 0", Clamp.ClampValue(-50.f), 0.f);
			TestEqual("above 'max' is NOT clamped", Clamp.ClampValue(9999.f), 9999.f);
		});

		It("clamps only the upper bound when bClampMin is disabled", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.bClampMin = false;
			Clamp.bClampMax = true;
			Clamp.Min = 0.f;   // Min is ignored — flag is off
			Clamp.Max = 100.f;
			TestEqual("below 'min' is NOT clamped", Clamp.ClampValue(-50.f), -50.f);
			TestEqual("above max clamped to 100", Clamp.ClampValue(9999.f), 100.f);
		});

		It("expresses a real [0,0] pin now that the sentinel is gone", [this]()
		{
			FAttributeClamp Clamp; // both flags default true
			Clamp.Min = 0.f;
			Clamp.Max = 0.f;
			TestEqual("positive value pinned to 0", Clamp.ClampValue(50.f), 0.f);
			TestEqual("negative value pinned to 0", Clamp.ClampValue(-50.f), 0.f);
		});

		It("clamps below-Min value up to Min", [this]()
		{
			FAttributeClamp Clamp;
			Clamp.Min = 10.f;
			Clamp.Max = 90.f;
			TestEqual("below min clamped to 10", Clamp.ClampValue(5.f), 10.f);
			TestEqual("large negative clamped to 10", Clamp.ClampValue(-9999.f), 10.f);
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
