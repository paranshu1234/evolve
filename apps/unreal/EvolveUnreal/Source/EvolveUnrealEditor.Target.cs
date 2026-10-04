using UnrealBuildTool;
public class EvolveUnrealEditorTarget : TargetRules
{
    public EvolveUnrealEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("EvolveUnreal");
    }
}
