// GMAS - GMC Ability System. MIT License, see LICENSE.

using System.IO;
using UnrealBuildTool;

public class GMCAbilitySystem : ModuleRules
{
	public GMCAbilitySystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"GMCCore",
				"EnhancedInput",
				"GameplayTasks",
				"GameplayTags",
				"StructUtils",
				"NetCore"
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"Niagara",
				"Projects",
				// Replay-burst diagnostics: settings registration + on-screen warning widget.
				"DeveloperSettings",
				"UMG"
			}
			);

		// The Gameplay Debugger category exists only where the debugger does (not in Shipping
		// or Test). Public because consumers that include GMCAbilitySystem.h see the header.
		if (Target.bBuildDeveloperTools || (Target.Configuration != UnrealTargetConfiguration.Shipping && Target.Configuration != UnrealTargetConfiguration.Test))
		{
			PublicDependencyModuleNames.Add("GameplayDebugger");
			PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_GAMEPLAY_DEBUGGER=0");
		}

		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AutomationController",
				"AutomationWorker"
			});
		}

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Components"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Attributes"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
	}
}
