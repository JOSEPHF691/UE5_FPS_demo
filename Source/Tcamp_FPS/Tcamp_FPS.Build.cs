// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class Tcamp_FPS : ModuleRules
{
	public Tcamp_FPS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[] {
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"EnhancedInput",
			"AIModule",
			"NavigationSystem",
			"StateTreeModule",
			"GameplayStateTreeModule",
			"UMG",
			"Slate",
			"SlateCore",
			"NetCore",
			"AnimGraphRuntime",
			"AnimationCore",
			"IKRig",
			"OnlineSubsystem",
			"OnlineSubsystemUtils"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { });

		PublicIncludePaths.AddRange(new string[] {
			"Tcamp_FPS",
			"Tcamp_FPS/Components",
			"Tcamp_FPS/Interfaces",
			"Tcamp_FPS/Variant_Shooter",
			"Tcamp_FPS/Variant_Shooter/AI",
			"Tcamp_FPS/Variant_Shooter/UI",
			"Tcamp_FPS/Variant_Shooter/Weapons",
			"Tcamp_FPS/Variant_Shooter/Animation",
			"Tcamp_FPS/Variant_Shooter/Economy",
			"Tcamp_FPS/Variant_Shooter/Armor",
			"Tcamp_FPS/Variant_Shooter/Treasure"
		});

		// Uncomment if you are using Slate UI
		// PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore" });

	}
}
