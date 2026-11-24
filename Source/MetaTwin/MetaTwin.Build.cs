using UnrealBuildTool;

public class MetaTwin : ModuleRules
{
	public MetaTwin(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput" });
		PrivateDependencyModuleNames.AddRange(new[] { "Json", "WebSockets", "UMG", "Slate", "SlateCore", "ImageCore" });
	}
}
