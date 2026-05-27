#include <Windows.h>
#include <chrono>
#include <algorithm>
#include "Engine/Core/Win32Window.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Asset/SceneSerializer.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Math/Math.h"
#include "Editor/EditorApp.h"

static Fujin::Win32Window    g_window;
static Fujin::GraphicsDevice g_gfx;
static Fujin::SceneManager   g_scene;
static Fujin::EditorApp      g_editor;
static Fujin::SceneRenderer  g_sceneRenderer;

static void SetupTestScene() {
    // Directional Light
    auto* dirLight = g_scene.CreateActor("DirectionalLight");
    auto* dlt = dirLight->AddComponent<Fujin::TransformComponent>();
    dlt->Position = Fujin::Vector3(0.0f, 10.0f, 0.0f);
    // X軸で-45度: lightDir=(0,-0.707,0.707) → 地面をNdotL=0.707で照らす
    dlt->Rotation = Fujin::Quaternion::FromEuler(0.0f, 0.0f, Fujin::Math::ToRadians(-45.0f));
    auto* dlc = dirLight->AddComponent<Fujin::LightComponent>();
    dlc->Type = Fujin::LightType::Directional;
    dlc->Color = Fujin::Vector3(1.0f, 0.95f, 0.8f);
    dlc->Intensity = 2.0f;

    // Point Light
    auto* pointLight = g_scene.CreateActor("PointLight");
    auto* plt = pointLight->AddComponent<Fujin::TransformComponent>();
    plt->Position = Fujin::Vector3(3.0f, 2.0f, 0.0f);
    auto* plc = pointLight->AddComponent<Fujin::LightComponent>();
    plc->Type = Fujin::LightType::Point;
    plc->Color = Fujin::Vector3(0.3f, 0.7f, 1.0f);
    plc->Intensity = 5.0f;
    plc->Range = 15.0f;

    // Cube (parent)
    auto* cube = g_scene.CreateActor("Cube");
    auto* ct = cube->AddComponent<Fujin::TransformComponent>();
    ct->Position = Fujin::Vector3(0.0f, 0.0f, 5.0f);
    auto* mc = cube->AddComponent<Fujin::MeshComponent>();
    mc->MeshPath     = "Resource/Meshes/cube.obj";
    mc->MaterialPath = "Resource/Materials/default.mat.json";

    // Child attached to Cube
    auto* child = g_scene.CreateActor("CubeChild");
    auto* cht = child->AddComponent<Fujin::TransformComponent>();
    cht->Position = Fujin::Vector3(2.0f, 0.0f, 0.0f);
    auto* cmc = child->AddComponent<Fujin::MeshComponent>();
    cmc->MeshPath = "Resource/Meshes/cube.obj";
    child->SetParent(cube);

    // Ground plane at y=0 (cube scaled flat, top surface = y 0)
    auto* ground = g_scene.CreateActor("Ground");
    auto* grndT = ground->AddComponent<Fujin::TransformComponent>();
    grndT->Position = Fujin::Vector3(0.0f, -0.5f, 5.0f);
    grndT->Scale    = Fujin::Vector3(20.0f, 1.0f, 20.0f);
    auto* grndM = ground->AddComponent<Fujin::MeshComponent>();
    grndM->MeshPath = "Resource/Meshes/cube.obj";

    // Run characters — 3 actors placed side by side with staggered phase offsets
    struct RunnerDesc { const char* name; float x; float offset; };
    constexpr RunnerDesc runners[] = {
        { "RunCharacter_A",  -2.0f, 0.00f },
        { "RunCharacter_B",   0.0f, 0.35f },
        { "RunCharacter_C",   2.0f, 0.70f },
    };
    for (auto& d : runners) {
        auto* actor = g_scene.CreateActor(d.name);
        auto* t = actor->AddComponent<Fujin::TransformComponent>();
        t->Position = Fujin::Vector3(d.x, 0.0f, 3.0f);
        auto* rmc = actor->AddComponent<Fujin::MeshComponent>();
        rmc->MeshPath = "Resource/Meshes/run.gltf";
        auto* anim = actor->AddComponent<Fujin::AnimationComponent>();
        anim->TimeOffset = d.offset;
    }

    // Steam emitter
    auto* steamActor = g_scene.CreateActor("SteamEmitter");
    auto* st = steamActor->AddComponent<Fujin::TransformComponent>();
    st->Position = Fujin::Vector3(-1.0f, 0.0f, 5.0f);
    auto* spc = steamActor->AddComponent<Fujin::ParticleComponent>();
    spc->AddEmitter(Fujin::MakeSteamPreset());

    // Spark emitter
    auto* sparkActor = g_scene.CreateActor("SparkEmitter");
    auto* spkt = sparkActor->AddComponent<Fujin::TransformComponent>();
    spkt->Position = Fujin::Vector3(1.0f, 0.5f, 5.0f);
    auto* spkpc = sparkActor->AddComponent<Fujin::ParticleComponent>();
    spkpc->AddEmitter(Fujin::MakeSparkPreset());

    // Beam emitter
    auto* beamActor = g_scene.CreateActor("BeamEmitter");
    auto* bt = beamActor->AddComponent<Fujin::TransformComponent>();
    bt->Position = Fujin::Vector3(3.0f, 0.0f, 5.0f);
    auto* bpc = beamActor->AddComponent<Fujin::ParticleComponent>();
    bpc->AddEmitter(Fujin::MakeBeamPreset());

    // GPU particle emitter
    auto* gpuActor = g_scene.CreateActor("GPUFireEmitter");
    auto* gpuT = gpuActor->AddComponent<Fujin::TransformComponent>();
    gpuT->Position = Fujin::Vector3(0.0f, 0.0f, 3.0f);
    auto* gpuPC = gpuActor->AddComponent<Fujin::ParticleComponent>();
    {
        Fujin::EmitterDesc gpuFire = Fujin::MakeFirePreset();
        gpuFire.Name        = "GPUFire";
        gpuFire.Simulation  = Fujin::SimMode::GPU;
        gpuFire.MaxParticles = 512;
        gpuPC->AddEmitter(gpuFire);
    }

    Fujin::SceneSerializer::Save(g_scene, "Resource/Scenes/test.scene.json");
}

static bool Init() {
    if (!g_window.Initialize(L"FujinEngine", 1280, 720))                                    return false;
    if (!g_gfx.Initialize(g_window.GetHWND(), g_window.GetWidth(), g_window.GetHeight()))   return false;
    if (!g_editor.Initialize(g_window.GetHWND(), g_gfx, g_scene))                           return false;
    if (!g_sceneRenderer.Initialize(g_gfx))                                                 return false;
    g_editor.SetMaterialManager(&g_sceneRenderer.GetMaterialManager());
    g_editor.SetSceneRenderer(&g_sceneRenderer);

    SetupTestScene(); // always regenerate to apply latest scene changes

    return true;
}

static void Run() {
    auto lastTime = std::chrono::steady_clock::now();

    while (g_window.ProcessMessages()) {
        auto now = std::chrono::steady_clock::now();
        float dt = (std::min)(std::chrono::duration<float>(now - lastTime).count(), 0.1f);
        lastTime = now;

        if (g_window.IsMinimized()) {
            Sleep(10);
            continue;
        }
        if (g_window.WasResized()) {
            uint32_t w = g_window.GetWidth();
            uint32_t h = g_window.GetHeight();
            if (w != g_gfx.GetWidth() || h != g_gfx.GetHeight()) {
                g_gfx.Resize(w, h);
                g_sceneRenderer.Resize(g_gfx, w, h);
            }
            g_window.ClearResize();
        }

        g_gfx.BeginFrame();

        // BeginFrame first: starts ImGui frame + updates debug camera from input
        g_editor.BeginFrame(dt);

        // Apply debug camera to renderer (zero lag — camera updated this frame)
        g_sceneRenderer.CameraPos    = g_editor.GetDebugCameraPos();
        g_sceneRenderer.CameraTarget = g_editor.GetDebugCameraTarget();

        uint32_t vpX, vpY, vpW, vpH;
        g_editor.GetViewportRect(vpX, vpY, vpW, vpH);

        g_sceneRenderer.Render(
            g_gfx.GetCommandList(),
            g_gfx,
            g_scene,
            g_gfx.GetWidth(),
            g_gfx.GetHeight(),
            g_gfx.GetCurrentFrameIndex(),
            vpX, vpY, vpW, vpH);

        // Pass viewProj to editor for camera gizmo projection
        g_editor.SetViewProj(g_sceneRenderer.GetLastViewProj());

        g_editor.Render(g_gfx.GetCommandList());

        g_gfx.EndFrame();
    }
}

static void Shutdown() {
    g_gfx.WaitForGPU();
    g_sceneRenderer.Shutdown();
    g_editor.Shutdown();
    g_gfx.Shutdown();
    g_window.Shutdown();
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    if (!Init()) return -1;
    Run();
    Shutdown();
    return 0;
}
