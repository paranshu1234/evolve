#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "core/Project.h"
#include "presentation/SceneBridge.h"
#include "EvolveScene.generated.h"

class UProceduralMeshComponent;
class UCameraComponent;

UCLASS()
class AEvolveScene : public AActor
{
    GENERATED_BODY()
public:
    AEvolveScene();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    FString Status() const;
private:
    void Rebuild();
    void Frame(bool SelectionOnly);
    void Pick();
    void UpdateCamera();
    UPROPERTY() UProceduralMeshComponent* Mesh;
    UPROPERTY() UCameraComponent* Camera;
    evolve::Project Project;
    evolve::SceneSnapshot Snapshot;
    size_t Selected = 0;
    bool Compare = false;
    bool Grid = true;
    bool MaterialReady = false;
    bool Smoke = false;
    int32 SmokeFrame = 0;
    double StartSeconds = 0;
    double MeshMilliseconds = 0;
    FVector Focus = FVector::ZeroVector;
    float Distance = 2200;
    float Yaw = -35;
    float Pitch = 12;
};

UCLASS()
class AEvolveHUD : public AHUD
{
    GENERATED_BODY()
public:
    virtual void DrawHUD() override;
};

UCLASS()
class AEvolveGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    AEvolveGameMode();
    virtual void BeginPlay() override;
};
