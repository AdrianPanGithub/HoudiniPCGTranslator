// Copyright (c) <2025> Yuzhe Pan (childadrianpan@gmail.com). All Rights Reserved.

using UnrealBuildTool;

public class HoudiniPCGTranslator : ModuleRules
{
	public HoudiniPCGTranslator(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				
			}
			);
			
		
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
                "HoudiniEngine",
				"PCG",
                "PCGGeometryScriptInterop"
            }
			);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{

			}
			);
	}
}
