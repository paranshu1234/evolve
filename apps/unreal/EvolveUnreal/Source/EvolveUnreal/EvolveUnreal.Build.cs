using UnrealBuildTool;
using System.IO;
public class EvolveUnreal : ModuleRules
{
    public EvolveUnreal(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        bUseUnity = false; // Keep the portable source translation units isolated.
        bEnableExceptions = true; // Project validates inputs with standard exceptions.
        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "ProceduralMeshComponent" });
        PublicIncludePaths.Add(Path.GetFullPath(Path.Combine(ModuleDirectory, "../../../../simulation/src")));
    }
}
