using UnrealBuildTool;

public class MetaTwinTarget : TargetRules
{
	public MetaTwinTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MetaTwin");
	}
}
