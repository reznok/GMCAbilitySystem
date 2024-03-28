using UnrealBuildTool;

public class GMCAbilitySystemEditor : ModuleRules
{
    public GMCAbilitySystemEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
                "GMCAbilitySystem",
                "PropertyEditor"
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "Slate",
                "SlateCore",
                "InputCore"
            }
        );
    }
}