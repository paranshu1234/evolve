#include "EvolveScene.h"
#include "ProceduralMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformMisc.h"

namespace {
FVector Position(evolve::Vec3 V) {
    const auto P = evolve::coreToUnrealPosition(V);
    return FVector(P.x, P.y, P.z);
}
FVector Direction(evolve::Vec3 V) {
    const auto P = evolve::coreToUnrealDirection(V);
    return FVector(P.x, P.y, P.z);
}
}

AEvolveScene::AEvolveScene()
{
    PrimaryActorTick.bCanEverTick = true;
    Mesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("DNA"));
    RootComponent = Mesh;
    Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("OrbitCamera"));
    Camera->SetupAttachment(RootComponent);
    Camera->SetFieldOfView(50.f);
}

void AEvolveScene::BeginPlay()
{
    Super::BeginPlay();
    StartSeconds = FPlatformTime::Seconds();
    Smoke = FParse::Param(FCommandLine::Get(), TEXT("EvolveSmokeTest"));
    auto* Material = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Evolve/Materials/M_DNA.M_DNA"));
    MaterialReady = Material != nullptr;
    if (Material) Mesh->SetMaterial(0, Material);
    Rebuild();
    Frame(false);
    if (auto* PC = GetWorld()->GetFirstPlayerController()) {
        PC->SetViewTarget(this);
        PC->bShowMouseCursor = true;
        PC->SetInputMode(FInputModeGameOnly());
    }
}

void AEvolveScene::Rebuild()
{
    check(IsInGameThread());
    const double Started = FPlatformTime::Seconds();
    Snapshot = evolve::buildSceneSnapshot(Project, Selected, Compare, Grid);
    TArray<FVector> Vertices, Normals;
    TArray<int32> Indices;
    TArray<FLinearColor> Colors;
    const int32 Count = static_cast<int32>(Snapshot.mesh.vertices.size());
    Vertices.Reserve(Count); Normals.Reserve(Count); Indices.Reserve(Count); Colors.Reserve(Count);
    for (const auto& V : Snapshot.mesh.vertices) {
        Vertices.Add(Position(V.position));
        Normals.Add(Direction(V.normal));
        Colors.Add(FLinearColor(V.color.x, V.color.y, V.color.z));
        Indices.Add(Indices.Num());
    }
    // Coordinate permutation has positive determinant: original triangle winding is retained.
    Mesh->CreateMeshSection_LinearColor(0, Vertices, Indices, Normals, TArray<FVector2D>(), Colors,
        TArray<FProcMeshTangent>(), false);
    MeshMilliseconds = (FPlatformTime::Seconds() - Started) * 1000.0;
}

void AEvolveScene::Frame(bool SelectionOnly)
{
    Focus = FVector::ZeroVector;
    if (SelectionOnly) {
        FVector Sum = FVector::ZeroVector;
        int32 Count = 0;
        for (const auto& P : Snapshot.mesh.pickPoints) if (P.index == Selected) { Sum += Position(P.position); ++Count; }
        if (Count) Focus = Sum / Count;
        Distance = 650.f;
    } else {
        // Helix framing deliberately excludes the optional large floor grid.
        const float Height = static_cast<float>(Project.sequence().size() - 1) * 62.f + 100.f;
        Distance = FMath::Max(1000.f, Height * 1.5f);
        Yaw = -35.f; Pitch = 12.f;
    }
    UpdateCamera();
}

void AEvolveScene::UpdateCamera()
{
    const FRotator Orbit(Pitch, Yaw, 0);
    const FVector Location = Focus - Orbit.Vector() * Distance;
    Camera->SetWorldLocation(Location);
    Camera->SetWorldRotation((Focus - Location).Rotation());
}

void AEvolveScene::Pick()
{
    auto* PC = GetWorld()->GetFirstPlayerController();
    float X, Y;
    if (!PC || !PC->GetMousePosition(X, Y)) return;
    float Best = 24.f * 24.f;
    size_t Candidate = Selected;
    for (const auto& Point : Snapshot.mesh.pickPoints) {
        FVector2D Screen;
        if (PC->ProjectWorldLocationToScreen(Position(Point.position), Screen)) {
            const float D = FVector2D::DistSquared(Screen, FVector2D(X, Y));
            if (D < Best) { Best = D; Candidate = Point.index; }
        }
    }
    if (Candidate != Selected) { Selected = Candidate; Rebuild(); }
}

void AEvolveScene::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    auto* PC = GetWorld()->GetFirstPlayerController();
    if (!PC) return;
    float DX, DY;
    PC->GetInputMouseDelta(DX, DY);
    if (PC->IsInputKeyDown(EKeys::RightMouseButton)) {
        Yaw += DX * 0.35f; Pitch = FMath::Clamp(Pitch + DY * 0.35f, -80.f, 80.f);
    }
    if (PC->IsInputKeyDown(EKeys::MiddleMouseButton))
        Focus += (-Camera->GetRightVector() * DX + Camera->GetUpVector() * DY) * Distance * 0.001f;
    if (PC->WasInputKeyJustPressed(EKeys::MouseScrollUp)) Distance *= 0.88f;
    if (PC->WasInputKeyJustPressed(EKeys::MouseScrollDown)) Distance *= 1.12f;
    Distance = FMath::Clamp(Distance, 150.f, 60000.f);
    if (PC->WasInputKeyJustPressed(EKeys::LeftMouseButton)) Pick();
    bool Dirty = false;
    const bool Ctrl = PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl);
    if (Ctrl && PC->WasInputKeyJustPressed(EKeys::Z)) Dirty |= Project.undo();
    if (Ctrl && PC->WasInputKeyJustPressed(EKeys::Y)) Dirty |= Project.redo();
    if (!Ctrl) {
        if (PC->WasInputKeyJustPressed(EKeys::A)) Dirty |= Project.edit(Selected, 'A');
        if (PC->WasInputKeyJustPressed(EKeys::C)) Dirty |= Project.edit(Selected, 'C');
        if (PC->WasInputKeyJustPressed(EKeys::G)) Dirty |= Project.edit(Selected, 'G');
        if (PC->WasInputKeyJustPressed(EKeys::T)) Dirty |= Project.edit(Selected, 'T');
    }
    if (PC->WasInputKeyJustPressed(EKeys::Left)) { Selected = (Selected + Project.sequence().size() - 1) % Project.sequence().size(); Dirty = true; }
    if (PC->WasInputKeyJustPressed(EKeys::Right)) { Selected = (Selected + 1) % Project.sequence().size(); Dirty = true; }
    if (PC->WasInputKeyJustPressed(EKeys::B)) { Compare = !Compare; Dirty = true; }
    if (PC->WasInputKeyJustPressed(EKeys::H)) { Grid = !Grid; Dirty = true; }
    if (PC->WasInputKeyJustPressed(EKeys::R)) Dirty |= Project.restoreBaseline();
    if (PC->WasInputKeyJustPressed(EKeys::SpaceBar)) Project.accept(Project.analyze());
    if (Dirty) Rebuild();
    if (PC->WasInputKeyJustPressed(EKeys::F)) Frame(false);
    if (PC->WasInputKeyJustPressed(EKeys::S)) Frame(true);
    UpdateCamera();
    if (Smoke) {
        ++SmokeFrame;
        if (SmokeFrame == 10) {
            const auto Original = Project.sequence();
            const auto OldResult = Project.analyze();
            const char Replacement = Original[0] == 'A' ? 'C' : 'A';
            bool OK = Project.edit(0, Replacement) && !Project.accept(OldResult);
            OK = Project.undo() && Project.sequence() == Original && OK;
            OK = Project.redo() && Project.sequence()[0] == Replacement && OK;
            OK = Project.accept(Project.analyze()) && OK;
            Compare = true; Rebuild(); Frame(false);
            OK = MaterialReady && !Snapshot.mesh.vertices.empty() && OK;
            const FString Report = FString::Printf(TEXT("%s\nrevision=%llu vertices=%d rebuild_ms=%.3f\nmock-only; microphone/cloud not linked\n"),
                OK ? TEXT("PASS") : TEXT("FAIL"), static_cast<unsigned long long>(Snapshot.revision),
                static_cast<int32>(Snapshot.mesh.vertices.size()), MeshMilliseconds);
            FFileHelper::SaveStringToFile(Report, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("EvolveSmokeTest.txt")));
            UE_LOG(LogTemp, Display, TEXT("EvolveSmokeTest: %s"), *Report);
            if (!OK) FPlatformMisc::RequestExitWithStatus(false, 1);
        }
        if (SmokeFrame == 60) PC->ConsoleCommand(TEXT("HighResShot 1"), true);
        if (SmokeFrame >= 180 && FPlatformTime::Seconds() - StartSeconds > 8.0) FPlatformMisc::RequestExit(false);
    }
}

FString AEvolveScene::Status() const
{
    FString Text = FString::Printf(TEXT("Base %d / %d: %c   revision %llu   undo %s / redo %s\n%s\nMesh: %d vertices | rebuild %.2f ms | frame %.2f ms\n%s"),
        static_cast<int32>(Selected + 1), static_cast<int32>(Project.sequence().size()), Project.sequence()[Selected],
        static_cast<unsigned long long>(Project.revision()), Project.canUndo() ? TEXT("yes") : TEXT("no"), Project.canRedo() ? TEXT("yes") : TEXT("no"),
        UTF8_TO_TCHAR(Project.sequence().c_str()), static_cast<int32>(Snapshot.mesh.vertices.size()), MeshMilliseconds,
        GetWorld()->GetDeltaSeconds() * 1000.0, MaterialReady ? TEXT("Schematic display units; not atomic coordinates") : TEXT("MATERIAL MISSING: run prepare_assets.py before packaging"));
    if (Project.hasResult()) Text += FString::Printf(TEXT("\nMOCK composition: GC %.1f%% -> %.1f%% | edits %d | no biological predictions"),
        Project.result().baselineGc, Project.result().scenarioGc, static_cast<int32>(Project.result().edits));
    return Text;
}

void AEvolveHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas) return;
    DrawRect(FLinearColor(0.015f, 0.025f, 0.045f, 0.9f), 12, 12, FMath::Min(Canvas->SizeX - 24.f, 950.f), 220);
    DrawText(TEXT("EVOLVE / UNREAL RENDERER POC"), FLinearColor(0.3f, 0.95f, 0.8f), 28, 24, nullptr, 1.3f);
    DrawText(TEXT("Synthetic DNA demo | Mock composition only | No biological predictions"), FLinearColor::White, 28, 54);
    DrawText(TEXT("RMB drag: orbit | MMB drag: pan | Wheel: zoom | F: frame all | S: frame selection\nClick or arrows: select | A/C/G/T: edit | Ctrl+Z/Y: undo/redo | B: compare | H: grid\nR: restore baseline | Space: mock composition | Close window to exit (session edits are not saved)"), FLinearColor(0.75f, 0.8f, 0.9f), 28, 78);
    for (TActorIterator<AEvolveScene> It(GetWorld()); It; ++It) {
        DrawText(It->Status(), FLinearColor::White, 28, 132); break;
    }
}

AEvolveGameMode::AEvolveGameMode()
{
    DefaultPawnClass = nullptr;
    HUDClass = AEvolveHUD::StaticClass();
}
void AEvolveGameMode::BeginPlay()
{
    Super::BeginPlay();
    GetWorld()->SpawnActor<AEvolveScene>();
    GetWorld()->SpawnActor<ADirectionalLight>(FVector(0, 0, 1000), FRotator(-45, -30, 0));
}
