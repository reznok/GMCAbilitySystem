using UnrealBuildTool;

public class GMCAbilitySystemAI : ModuleRules
{
    public GMCAbilitySystemAI(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "StateTreeModule",
                "GameplayStateTreeModule",
                "GMCAbilitySystem",
                "GMCCore",
                "AIModule",
                "GameplayTags",
                "GameplayTasks"
            }
        );
    }
}