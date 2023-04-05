// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class TrueFPSSystem : ModuleRules
{
	public TrueFPSSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject", 
				"Engine", 
				"InputCore", 
				"NavigationSystem",
				"AIModule",
				"PhysicsCore",
				"AnimationCore",
				"AnimGraphRuntime", 
				"AnimGraph", 
				"GameplayTags", 
				"GameplayTasks",
				"Niagara",
				"EnhancedInput"
			}
			);
			
		
		// PrivateDependencyModuleNames.AddRange(
		// 	new string[]
		// 	{
		// 		"Slate",
		// 		"SlateCore",
		// 		"NetCore",
		// 	}
		// 	);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
