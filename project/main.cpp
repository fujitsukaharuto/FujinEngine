#include <Windows.h>
#include <chrono>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include <string>
#include <cmath>
#include "Engine/Core/Win32Window.h"
#include "Engine/Core/SceneManager.h"
#include "Engine/Core/TransformComponent.h"
#include "Engine/Core/MeshComponent.h"
#include "Engine/Core/LightComponent.h"
#include "Engine/Core/AnimationComponent.h"
#include "Engine/Core/ParticleComponent.h"
#include "Engine/Core/ColliderComponent.h"
#include "Engine/Core/RigidbodyComponent.h"
#include "Engine/Core/RotatorComponent.h"
#include "Engine/Core/PlayerMovementComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Asset/SceneSerializer.h"
#include "Engine/Graphics/GraphicsDevice.h"
#include "Engine/Renderer/SceneRenderer.h"
#include "Engine/Physics/PhysicsWorld.h"
#include "Engine/Math/Math.h"
#include "Editor/EditorApp.h"

static Fujin::Win32Window    g_window;
static Fujin::GraphicsDevice g_gfx;
static Fujin::SceneManager   g_scene;
static Fujin::EditorApp      g_editor;
static Fujin::SceneRenderer  g_sceneRenderer;
static Fujin::PhysicsWorld   g_physics;

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
    // Tick-layer demo: spins the cube (and its parented child orbits with it) while in Play.
    cube->AddComponent<Fujin::RotatorComponent>()->DegreesPerSecond = 60.0f;

    // Input-system demo: a player-controlled cube. In Play, WASD / left stick move it and Space /
    // gamepad A makes it hop (see PlayerMovementComponent + the default mappings in Init).
    // Demo only — delete this block (and the include) to remove.
    auto* player = g_scene.CreateActor("Player");
    auto* pt = player->AddComponent<Fujin::TransformComponent>();
    pt->Position = Fujin::Vector3(-4.0f, 0.0f, 5.0f);
    auto* pmc = player->AddComponent<Fujin::MeshComponent>();
    pmc->MeshPath     = "Resource/Meshes/cube.obj";
    pmc->MaterialPath = "Resource/Materials/default.mat.json";
    player->AddComponent<Fujin::PlayerMovementComponent>();

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
    // Static floor collider so dropped bodies come to rest (box, no rigidbody = immovable).
    auto* grndCol = ground->AddComponent<Fujin::ColliderComponent>();
    grndCol->Shape   = Fujin::ColliderShape::AABB;
    grndCol->Channel = Fujin::CollisionChannel::WorldStatic;

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

    // ── OBB test: a TILTED BOX ramp with a sphere that rolls/slides down it ──
    // Exercises oriented sphere-vs-box collision (a rotated box collider is now respected,
    // instead of being treated as axis-aligned). Press Play and watch the ball follow the
    // tilted surface down toward the floor. The ramp is static (no RigidbodyComponent).
    auto* ramp = g_scene.CreateActor("Ramp");
    auto* rmpT = ramp->AddComponent<Fujin::TransformComponent>();
    rmpT->Position = Fujin::Vector3(6.0f, 1.0f, 8.0f);
    rmpT->Rotation = Fujin::Quaternion::FromAxisAngle(Fujin::Vector3(0, 0, 1), Fujin::Math::ToRadians(25.0f));
    rmpT->Scale    = Fujin::Vector3(5.0f, 0.4f, 3.0f);   // wide thin slab
    auto* rmpM = ramp->AddComponent<Fujin::MeshComponent>();
    rmpM->MeshPath     = "Resource/Meshes/cube.obj";
    rmpM->MaterialPath = "Resource/Materials/default.mat.json";
    auto* rmpCol = ramp->AddComponent<Fujin::ColliderComponent>();
    rmpCol->Shape   = Fujin::ColliderShape::AABB;        // box; orientation comes from the transform
    rmpCol->Channel = Fujin::CollisionChannel::WorldStatic;

    // Sphere collider (works against the oriented box) dropped above the ramp's high (+X) side.
    // Visual mesh is the unit cube (no sphere mesh in the project); the collider is a sphere,
    // so you also see physics spin the body as it rolls.
    auto* ball = g_scene.CreateActor("RampBall");
    auto* ballT = ball->AddComponent<Fujin::TransformComponent>();
    ballT->Position = Fujin::Vector3(7.5f, 4.0f, 7.0f);
    auto* ballM = ball->AddComponent<Fujin::MeshComponent>();
    ballM->MeshPath     = "Resource/Meshes/cube.obj";
    ballM->MaterialPath = "Resource/Materials/default.mat.json";
    auto* ballCol = ball->AddComponent<Fujin::ColliderComponent>();
    ballCol->Shape  = Fujin::ColliderShape::Sphere;
    ballCol->Radius = 0.5f;
    auto* ballRb = ball->AddComponent<Fujin::RigidbodyComponent>();
    ballRb->Mass        = 1.0f;
    ballRb->Restitution = 0.2f;
    ballRb->Friction    = 0.4f;

    // ── OBB box×box test: a dynamic BOX dropped onto the tilted ramp. With oriented box-box
    // collision it should settle FLUSH against the slope (tilted ~25°), not pass through or
    // lie flat. (z-lane separated from the ball above.) ──
    auto* rampBox = g_scene.CreateActor("RampBox");
    auto* rbxT = rampBox->AddComponent<Fujin::TransformComponent>();
    rbxT->Position = Fujin::Vector3(6.5f, 4.0f, 9.0f);
    rbxT->Scale    = Fujin::Vector3(0.8f, 0.8f, 0.8f);
    auto* rbxM = rampBox->AddComponent<Fujin::MeshComponent>();
    rbxM->MeshPath     = "Resource/Meshes/cube.obj";
    rbxM->MaterialPath = "Resource/Materials/default.mat.json";
    auto* rbxCol = rampBox->AddComponent<Fujin::ColliderComponent>();
    rbxCol->Shape = Fujin::ColliderShape::AABB;
    auto* rbxRb = rampBox->AddComponent<Fujin::RigidbodyComponent>();
    rbxRb->Mass        = 1.0f;
    rbxRb->Restitution = 0.1f;
    rbxRb->Friction    = 0.6f;

    // ── Axis-aligned stacking regression: two dynamic boxes dropped on the flat floor at x=-5.
    // They should settle into a stable, non-rocking stack (OBB path must not regress AABB stacks). ──
    for (int i = 0; i < 2; ++i) {
        auto* sb = g_scene.CreateActor(i == 0 ? "StackBoxLower" : "StackBoxUpper");
        auto* sbT = sb->AddComponent<Fujin::TransformComponent>();
        sbT->Position = Fujin::Vector3(-5.0f, 1.0f + i * 1.3f, 8.0f);
        auto* sbM = sb->AddComponent<Fujin::MeshComponent>();
        sbM->MeshPath     = "Resource/Meshes/cube.obj";
        sbM->MaterialPath = "Resource/Materials/default.mat.json";
        auto* sbCol = sb->AddComponent<Fujin::ColliderComponent>();
        sbCol->Shape = Fujin::ColliderShape::AABB;
        auto* sbRb = sb->AddComponent<Fujin::RigidbodyComponent>();
        sbRb->Mass     = 1.0f;
        sbRb->Friction = 0.6f;
    }

    // ── Stress / visual: a CLUSTER of many sphere-collider bodies poured onto the tilted ramp.
    // Exercises the BVH broadphase, sphere-vs-OBB (the tilted ramp) and sphere-sphere at scale —
    // the spheres cascade down the slope and pile on the floor. ──
    {
        constexpr int NX = 4, NY = 4, NZ = 4;   // 64 spheres
        int idx = 0;
        for (int ix = 0; ix < NX; ++ix)
        for (int iy = 0; iy < NY; ++iy)
        for (int iz = 0; iz < NZ; ++iz) {
            auto* sp = g_scene.CreateActor("RampSphere_" + std::to_string(idx));
            auto* spT = sp->AddComponent<Fujin::TransformComponent>();
            float jitter = ((idx * 7) % 5) * 0.01f;   // tiny offset to break perfect symmetry
            spT->Position = Fujin::Vector3(5.6f + ix * 0.5f + jitter,
                                           4.2f + iy * 0.55f,
                                           6.9f + iz * 0.5f);
            spT->Scale = Fujin::Vector3(0.4f, 0.4f, 0.4f);
            auto* spM = sp->AddComponent<Fujin::MeshComponent>();
            spM->MeshPath     = "Resource/Meshes/cube.obj";   // no sphere mesh; collider is a sphere
            spM->MaterialPath = "Resource/Materials/default.mat.json";
            auto* spCol = sp->AddComponent<Fujin::ColliderComponent>();
            spCol->Shape  = Fujin::ColliderShape::Sphere;
            spCol->Radius = 0.2f;
            auto* spRb = sp->AddComponent<Fujin::RigidbodyComponent>();
            spRb->Mass        = 0.5f;
            spRb->Restitution = 0.2f;
            spRb->Friction    = 0.4f;
            ++idx;
        }
    }

    // ── Clustered-lighting stress: a grid of many small point lights over the ground.
    // Far more than the old 16-light cap; the clustered culling keeps each pixel cheap. ──
    {
        constexpr int LGX = 8, LGZ = 8;   // 64 point lights
        int li = 0;
        for (int gx = 0; gx < LGX; ++gx)
        for (int gz = 0; gz < LGZ; ++gz) {
            auto* pl = g_scene.CreateActor("GridLight_" + std::to_string(li));
            auto* glT = pl->AddComponent<Fujin::TransformComponent>();
            glT->Position = Fujin::Vector3(-8.0f + gx * 2.3f, 1.0f, -3.0f + gz * 2.3f);
            auto* glc = pl->AddComponent<Fujin::LightComponent>();
            glc->Type      = Fujin::LightType::Point;
            // Cycle hues so the grid is visually distinct.
            float h = (float)li / (float)(LGX * LGZ);
            glc->Color     = Fujin::Vector3(0.5f + 0.5f * std::sin(h * 6.2831f),
                                            0.5f + 0.5f * std::sin(h * 6.2831f + 2.094f),
                                            0.5f + 0.5f * std::sin(h * 6.2831f + 4.188f));
            glc->Intensity = 3.0f;
            glc->Range     = 2.8f;
            ++li;
        }
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

    // Input system: device polling + UE5-style named axis/action mappings (default scheme).
    Fujin::Input& input = Fujin::Input::Get();
    input.Initialize(g_window.GetHWND());
    input.BindAxisKey("MoveForward", Fujin::Key::W, +1.0f);
    input.BindAxisKey("MoveForward", Fujin::Key::S, -1.0f);
    input.BindAxisPad("MoveForward", Fujin::PadAxis::LeftY, +1.0f);
    input.BindAxisKey("MoveRight", Fujin::Key::D, +1.0f);
    input.BindAxisKey("MoveRight", Fujin::Key::A, -1.0f);
    input.BindAxisPad("MoveRight", Fujin::PadAxis::LeftX, +1.0f);
    input.BindAction("Jump", Fujin::Key::Space);
    input.BindActionPad("Jump", Fujin::PadButton::A);

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

        // Physics step (only while playing); snapshot/restore transforms on play/stop
        const bool isPlaying = g_editor.IsPlaying();
        static bool s_wasPlaying = false;

        struct TransformSnapshot { Fujin::Vector3 pos; Fujin::Quaternion rot; Fujin::Vector3 scale; };
        static std::unordered_map<uint64_t, TransformSnapshot> s_snapshot;

        if (isPlaying && !s_wasPlaying) {
            // Play started: save all transforms, then fire BeginPlay on every component.
            s_snapshot.clear();
            for (auto& actor : g_scene.GetActors()) {
                auto* tc = actor->GetComponent<Fujin::TransformComponent>();
                if (tc) s_snapshot[actor->GetId()] = { tc->Position, tc->Rotation, tc->Scale };
            }
            g_scene.BeginPlay();
        }
        if (!isPlaying && s_wasPlaying) {
            // Stop: fire EndPlay, restore all transforms, then reset physics state
            g_scene.EndPlay();
            for (auto& actor : g_scene.GetActors()) {
                auto* tc = actor->GetComponent<Fujin::TransformComponent>();
                auto it  = s_snapshot.find(actor->GetId());
                if (tc && it != s_snapshot.end()) {
                    tc->Position = it->second.pos;
                    tc->Rotation = it->second.rot;
                    tc->Scale    = it->second.scale;
                }
            }
            g_physics.Reset(g_scene);
        }
        s_wasPlaying = isPlaying;

        // Poll input every frame (keeps press/release edges coherent). Gate gameplay queries to Play
        // mode AND window focus so the editor / background never drives gameplay.
        Fujin::Input::Get().SetEnabled(isPlaying && GetForegroundWindow() == g_window.GetHWND());
        Fujin::Input::Get().Update();

        // Refresh cached world transforms (editor edits, parenting) before gameplay/physics read them.
        g_scene.UpdateWorldTransforms();

        if (isPlaying) {
            g_scene.Update(dt);              // gameplay tick (PrePhysics): may move actors / apply forces
            g_scene.UpdateWorldTransforms(); // reflect gameplay edits before physics reads world
            g_physics.Step(g_scene, dt);
        }

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

        // Pass view/proj to editor for gizmo and camera gizmo projection
        g_editor.SetViewAndProj(g_sceneRenderer.GetLastView(), g_sceneRenderer.GetLastProj());

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
