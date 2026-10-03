// GMAS - GMC Ability System. MIT License, see LICENSE.

using UnrealBuildTool;

// Specs and the test-only stub classes. UncookedOnly: built for the editor and uncooked
// targets, never into a cooked or Shipping game, so no reflected test class ships.
public class GMCAbilitySystemTests : ModuleRules
{
	public GMCAbilitySystemTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GMCAbilitySystem",
			"GMCCore",
			"GameplayTags",
			"GameplayTasks",
			"EnhancedInput",
			"StructUtils",
			"DeveloperSettings"
		});
	}
}
