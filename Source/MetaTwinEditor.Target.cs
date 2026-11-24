using UnrealBuildTool;

public class MetaTwinEditorTarget : TargetRules
{
	public MetaTwinEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("MetaTwin");
	}
}
