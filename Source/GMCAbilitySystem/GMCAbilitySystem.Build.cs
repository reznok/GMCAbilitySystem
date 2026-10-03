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

		// The Gameplay Debugger category exists only where the debugger does. The engine helper
		// adds the dependency (public: consumers that include GMCAbilitySystem.h see the header)
		// and defines WITH_GAMEPLAY_DEBUGGER* exactly as the target's settings say, so GMAS can
		// never disagree with the value it already inherits through GMCCore -> AIModule.
		SetupGameplayDebuggerSupport(Target, /*bAddAsPublicDependency*/ true);

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Components"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Attributes"));
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
	}
}
