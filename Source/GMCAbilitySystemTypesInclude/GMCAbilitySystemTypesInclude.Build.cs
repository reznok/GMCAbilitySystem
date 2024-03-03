// Copyright Epic Games, Inc. All Rights Reserved.

using System.IO;
using UnrealBuildTool;

public class GMCAbilitySystemTypesInclude : ModuleRules
{
	public GMCAbilitySystemTypesInclude(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"StructUtils",
				"CoreUObject",
			}
			);
		
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/SyncTypes"));
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public/Include"));
		
		PrivateIncludePaths.Add(Path.Combine(ModuleDirectory, "Private"));
		
		PublicDefinitions.Add("GMC_ENABLE_USER_SYNC_TYPES");
		PublicDefinitions.Add("GMC_USER_INCLUDE_PATH_TYPES     = \"Types.user\"");
		PublicDefinitions.Add("GMC_USER_INCLUDE_PATH_TYPE_LIST = \"STList.user\"");
		PublicDefinitions.Add("GMC_USER_INCLUDE_PATH_TYPE_INTF = \"STIntf.user\"");
		PublicDefinitions.Add("GMC_USER_INCLUDE_PATH_TYPE_IMPL = \"STImpl.user\"");
	}
}
