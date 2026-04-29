// Copyright Epic Games, Inc. All Rights Reserved.

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
				"GameplayDebugger",
				"StructUtils",
				"NetCore"
				// ... add other public dependencies that you statically link with here ...
			}
			);
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore", "Niagara",
				// Replay-burst diagnostics: settings registration + on-screen warning widget.
				"DeveloperSettings",
				"UMG"
				// ... add private dependencies that you statically link with here ...
			}
			);
		
		if (Target.Configuration != UnrealTargetConfiguration.Shipping)
		{
			PrivateDependencyModuleNames.AddRange(new string[] {
				"AutomationController",
				"AutomationWorker"
			});
		}
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Components"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Attributes"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		// Expose test helper headers (e.g. UGMAS_TestDelayAbility) so Layer-3
		// functional tests compiled into the game module can include them.
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Private/Tests"));
	}
}
