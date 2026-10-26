// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class DeftBlade : ModuleRules
{
	public DeftBlade(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivateIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "AIModule", "NavigationSystem", "UMG" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

		if (Target.bBuildEditor)
		{
			// PIE integration tests only; never link UnrealEd into the Game target.
			PrivateDependencyModuleNames.Add("UnrealEd");
		}
	}
}
