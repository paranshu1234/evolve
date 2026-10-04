using UnrealBuildTool;
public class EvolveUnrealTarget : TargetRules
{
    public EvolveUnrealTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        ExtraModuleNames.Add("EvolveUnreal");
    }
}
