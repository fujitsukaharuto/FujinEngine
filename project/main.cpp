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
#include "Engine/Core/CharacterMovementComponent.h"
#include "Engine/Core/PawnComponent.h"
#include "Engine/Core/PlayerControllerComponent.h"
#include "Engine/Core/PlayerStartComponent.h"
#include "Engine/Core/FootIKComponent.h"
#include "Engine/Core/TimerDemoComponent.h"
#include "Engine/Input/Input.h"
#include "Engine/Asset/SceneSerializer.h"
#include "Engine/Asset/SaveSystem.h"
#include "Engine/Core/SaveGame.h"
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

    // Child attached to Cube
    auto* child = g_scene.CreateActor("CubeChild");
    auto* cht = child->AddComponent<Fujin::TransformComponent>();
    cht->Position = Fujin::Vector3(2.0f, 0.0f, 0.0f);
    auto* cmc = child->AddComponent<Fujin::MeshComponent>();
    cmc->MeshPath = "Resource/Meshes/cube.obj";
    child->SetParent(cube);

    // Event/Timer demo: a cube that "pops" its scale every Interval seconds, driven by a looping
    // world timer firing a multicast OnPulse event (see TimerDemoComponent). Demo only — delete this
    // block (and the include) to remove. In Play the cube blinks bigger/smaller in discrete steps.
    auto* timerCube = g_scene.CreateActor("TimerCube");
    auto* tct = timerCube->AddComponent<Fujin::TransformComponent>();
    tct->Position = Fujin::Vector3(-4.0f, 1.0f, 5.0f);
    tct->Scale    = Fujin::Vector3(0.5f, 0.5f, 0.5f);
    auto* tcm = timerCube->AddComponent<Fujin::MeshComponent>();
    tcm->MeshPath     = "Resource/Meshes/cube.obj";
    tcm->MaterialPath = "Resource/Materials/default.mat.json";
    timerCube->AddComponent<Fujin::TimerDemoComponent>();

    // Ground slab whose TOP surface sits exactly at y=0. cube.obj spans ±1, so at scale.y=1 the
    // mesh is 2u tall; positioning the center at y=-1 puts the top face at 0 (and the bottom at -2).
    auto* ground = g_scene.CreateActor("Ground");
    auto* grndT = ground->AddComponent<Fujin::TransformComponent>();
    grndT->Position = Fujin::Vector3(0.0f, -1.0f, 5.0f);
    grndT->Scale    = Fujin::Vector3(20.0f, 1.0f, 20.0f);
    auto* grndM = ground->AddComponent<Fujin::MeshComponent>();
    grndM->MeshPath = "Resource/Meshes/cube.obj";
    // Static floor collider so dropped bodies come to rest (box, no rigidbody = immovable).
    // HalfExtents.y=1 (×scale.y=1) makes the collider 2u tall too, so its top face matches the mesh at y=0.
    auto* grndCol = ground->AddComponent<Fujin::ColliderComponent>();
    grndCol->Shape         = Fujin::ColliderShape::AABB;
    grndCol->HalfExtents.y = 1.0f;
    grndCol->Channel       = Fujin::CollisionChannel::WorldStatic;

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

    // Playable Fox — anim state-graph + character movement demo (the engine's UE5 "Character" analog).
    // In Play, WASD / left stick walk the Fox on the floor; its horizontal speed drives a 1D blend
    // space (Survey idle → Walk → Run) and the body turns to face the movement direction.
    {
        auto* fox = g_scene.CreateActor("Fox");
        auto* foxT = fox->AddComponent<Fujin::TransformComponent>();
        foxT->Position = Fujin::Vector3(-2.0f, 0.0f, 7.0f);
        foxT->Scale    = Fujin::Vector3(0.02f, 0.02f, 0.02f);   // Fox model is ~100u tall; adjust on /run
        auto* foxM = fox->AddComponent<Fujin::MeshComponent>();
        foxM->MeshPath = "Resource/Meshes/Fox.gltf";

        auto* foxA = fox->AddComponent<Fujin::AnimationComponent>();
        foxA->UseBlendSpace = true;
        foxA->Blend.Param   = "Speed";
        foxA->Blend.Samples = {
            { "Survey", 0.0f },   // idle look-around
            { "Walk",   1.5f },
            { "Run",    4.0f },
        };

        auto* foxMove = fox->AddComponent<Fujin::CharacterMovementComponent>();
        foxMove->MaxSpeed = 4.0f;   // Sprint (Shift) → Run threshold; plain WASD walks at WalkSpeed

        // Make the Fox a possessable Pawn (UE5 framework layer). At BeginPlay the auth GameMode spawns
        // a PlayerController and possesses this pawn (AutoPossessPlayer), so the controller reads input
        // and feeds the pawn, and CharacterMovementComponent consumes the pawn's input instead of
        // reading Input directly — controller → pawn → movement. Remove this to fall back to the
        // pawn-less path (movement reads Input itself), which behaves identically.
        auto* foxPawn = fox->AddComponent<Fujin::PawnComponent>();
        foxPawn->AutoPossessPlayer = false;   // CesiumMan below is now the player; Fox is a free roaming AI pawn

        // Foot IK: plant the Fox's 4 paws on the ground (steps/slopes). Quadruped chains are
        // upper→lower→paw; the front "arms" and rear "legs" of the glTF Fox skeleton.
        auto* foxIK = fox->AddComponent<Fujin::FootIKComponent>();
        // Relative foot IK (shift each foot only by how far its ground deviates from the body's base
        // plane), so flat ground needs no offset tuning. FootOffset / per-leg Offset stay 0 unless a
        // foot needs a small fine-tune on uneven terrain.
        foxIK->PelvisBone = "b_Hip_01";
        foxIK->Legs = {
            { "b_RightUpperArm_06", "b_RightForeArm_07", "b_RightHand_08",    0.0f }, // front-right
            { "b_LeftUpperArm_09",  "b_LeftForeArm_010", "b_LeftHand_011",    0.0f }, // front-left
            { "b_LeftLeg01_015",    "b_LeftLeg02_016",   "b_LeftFoot01_017",  0.0f }, // rear-left
            { "b_RightLeg01_019",   "b_RightLeg02_020",  "b_RightFoot01_021", 0.0f }, // rear-right
        };

        // Bone socket demo (UE5 attach-to-bone): a small cube parented to the Fox and attached to its
        // front-right paw bone, so it rides that bone through the walk/run/idle animation (the
        // weapon-in-hand case). The child's local transform is relative to the posed bone (mesh-space,
        // ~100u model), so it's scaled up here then brought to world size by the Fox's 0.02 scale.
        // Clearing AttachSocket falls back to plain parent-root attachment. Scale/offset tunable on /run.
        {
            auto* held  = g_scene.CreateActor("FoxHandCube");
            auto* heldT = held->AddComponent<Fujin::TransformComponent>();
            heldT->AttachSocket = "b_RightHand_08";                  // front-right paw bone
            heldT->Scale        = Fujin::Vector3(10.0f, 10.0f, 10.0f);
            auto* heldM = held->AddComponent<Fujin::MeshComponent>();
            heldM->MeshPath     = "Resource/Meshes/cube.obj";
            heldM->MaterialPath = "Resource/Materials/default.mat.json";
            held->SetParent(fox);
        }
    }

    // Playable CesiumMan — the human "Character" the player possesses (UE5 third-person template).
    // Placed apart from the Fox so both are visible; WASD / left stick walk it on the floor while its
    // single walk clip loops. CesiumMan is a humanoid biped (glTF is ~1.8u tall, real-world metres),
    // so it needs no down-scaling like the Fox; tune Scale/Position on /run if needed.
    {
        auto* man = g_scene.CreateActor("CesiumMan");
        auto* manT = man->AddComponent<Fujin::TransformComponent>();
        manT->Position = Fujin::Vector3(0.0f, 0.0f, 4.0f);
        manT->Scale    = Fujin::Vector3(1.0f, 1.0f, 1.0f);
        auto* manM = man->AddComponent<Fujin::MeshComponent>();
        manM->MeshPath = "Resource/Meshes/CesiumMan.gltf";

        // CesiumMan ships a single (walk) clip — no Survey/Walk/Run set, so play it directly
        // (ClipName "" = first clip found) and loop it rather than build a blend space.
        auto* manA = man->AddComponent<Fujin::AnimationComponent>();
        manA->Loop = true;

        auto* manMove = man->AddComponent<Fujin::CharacterMovementComponent>();
        manMove->MaxSpeed = 4.0f;

        // This is the possessed player: at BeginPlay the GameMode spawns a PlayerController and
        // possesses this pawn (controller → pawn → movement). The Fox above has AutoPossessPlayer off.
        auto* manPawn = man->AddComponent<Fujin::PawnComponent>();
        manPawn->AutoPossessPlayer = true;

        // Foot IK: plant CesiumMan's 2 feet on the ground (steps/slopes). Biped chains are
        // thigh→shin→foot; the toe joint (leg_joint_*_5) sits below the foot and isn't part of the
        // two-bone IK. Pelvis is the root joint both legs hang from. Relative IK, so flat ground needs
        // no offset tuning; per-leg Offset stays 0 unless a foot needs a fine-tune on uneven terrain.
        auto* manIK = man->AddComponent<Fujin::FootIKComponent>();
        manIK->PelvisBone = "Skeleton_torso_joint_1";
        manIK->Legs = {
            { "leg_joint_R_1", "leg_joint_R_2", "leg_joint_R_3", 0.0f }, // right leg
            { "leg_joint_L_1", "leg_joint_L_2", "leg_joint_L_3", 0.0f }, // left leg
        };
    }

    // PlayerStart — spawn point for the GameMode's default pawn (UE5 APlayerStart). It's an empty
    // marker (no mesh): the GameMode spawns DefaultPawnSpawner's pawn here ONLY when no pawn is placed
    // with AutoPossessPlayer. With CesiumMan above auto-possessed, this stays dormant; uncheck
    // CesiumMan's "Auto Possess Player" in the Inspector (or delete it) and Play will spawn the default
    // pawn at this transform instead. Demo only — delete to remove.
    {
        auto* start = g_scene.CreateActor("PlayerStart");
        auto* startT = start->AddComponent<Fujin::TransformComponent>();
        startT->Position = Fujin::Vector3(2.0f, 0.0f, 7.0f);
        start->AddComponent<Fujin::PlayerStartComponent>();
    }

    // Demo staircase in front of the Fox (+Z is "forward"/W) so step-up is visible: 4 static steps
    // rising 0.3 each — under the Fox's MaxStepHeight (0.4), so it walks up without jumping; a step
    // taller than that would be blocked like a wall. Static slabs from y=0 to their top, collider
    // sized to match the mesh (cube.obj spans ±1 ⇒ HalfExtents 1). Demo only — delete to remove.
    for (int i = 0; i < 4; ++i) {
        float topY = 0.3f * (i + 1);
        auto* step = g_scene.CreateActor("Step_" + std::to_string(i));
        auto* stp = step->AddComponent<Fujin::TransformComponent>();
        stp->Position = Fujin::Vector3(-2.0f, topY * 0.5f, 9.0f + i * 0.8f);
        stp->Scale    = Fujin::Vector3(0.9f, topY * 0.5f, 0.4f);
        auto* sm = step->AddComponent<Fujin::MeshComponent>();
        sm->MeshPath     = "Resource/Meshes/cube.obj";
        sm->MaterialPath = "Resource/Materials/default.mat.json";
        auto* sc = step->AddComponent<Fujin::ColliderComponent>();
        sc->Shape       = Fujin::ColliderShape::AABB;
        sc->HalfExtents = Fujin::Vector3(1.0f, 1.0f, 1.0f);   // match cube.obj (±1) so top = mesh top
        sc->Channel     = Fujin::CollisionChannel::WorldStatic;
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
    input.BindAction("Sprint", Fujin::Key::Shift);              // hold to run (else WASD walks)
    input.BindActionPad("Sprint", Fujin::PadButton::LeftShoulder);

    g_scene.SetPhysicsWorld(&g_physics); // let gameplay (e.g. character ground traces) query physics

    // GameMode's DefaultPawnClass analog (UE5): a factory the GameMode runs to build a pawn when the
    // scene has no placed AutoPossessPlayer pawn, spawning it at the PlayerStart. Runtime wiring, not
    // serialized — set here. This builds a simple cube character (mesh + movement + pawn); the Fox in
    // SetupTestScene normally wins, so this only fires if you clear the Fox's AutoPossessPlayer flag.
    g_scene.GetAuthGameMode().DefaultPawnSpawner = [](Fujin::Actor& a) {
        a.AddComponent<Fujin::TransformComponent>();   // pose is set by the GameMode to the PlayerStart
        auto* m = a.AddComponent<Fujin::MeshComponent>();
        m->MeshPath     = "Resource/Meshes/cube.obj";
        m->MaterialPath = "Resource/Materials/default.mat.json";
        auto* mv = a.AddComponent<Fujin::CharacterMovementComponent>();
        mv->MaxSpeed = 4.0f;
        a.AddComponent<Fujin::PawnComponent>();
    };

    // Scene is now an asset (Resource/Scenes/test.scene.json): load it so in-editor edits / Duplicate
    // / "Save Scene" persist across runs. SetupTestScene is only the first-run bootstrap (it also saves
    // the file). Delete the .json to regenerate from code. The scene fully round-trips now — anim blend
    // spaces / state machines and foot-IK leg chains serialize, so loading rebuilds them too.
    if (!Fujin::SceneSerializer::Load(g_scene, "Resource/Scenes/test.scene.json"))
        SetupTestScene();

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

        // ── Quick save / load demo (F5 = save slot "quicksave", F9 = load it). Captures the live
        // scene — during Play that's the current gameplay positions — into Saves/quicksave.save.json
        // plus a tiny SaveGame data bag. On load the scene is rebuilt, so we clear editor selection,
        // refresh world transforms and physics, and (if playing) re-fire BeginPlay + re-snapshot so
        // loaded actors tick and Stop restores to the loaded state. Demo only — delete this block. ──
        {
            const bool focused = GetForegroundWindow() == g_window.GetHWND();
            static bool s_f5Down = false, s_f9Down = false;
            const bool f5 = focused && (GetAsyncKeyState(VK_F5) & 0x8000) != 0;
            const bool f9 = focused && (GetAsyncKeyState(VK_F9) & 0x8000) != 0;

            if (f5 && !s_f5Down) {
                static int s_saveCount = 0;
                Fujin::SaveGame sg;
                sg.SetInt("saveCount", ++s_saveCount);
                sg.SetBool("wasPlaying", isPlaying);
                const bool ok = Fujin::SaveSystem::SaveToSlot("quicksave", g_scene, &sg);
                OutputDebugStringA(ok ? "[SaveSystem] quicksave written\n"
                                      : "[SaveSystem] save FAILED\n");
            }
            if (f9 && !s_f9Down) {
                Fujin::SaveGame sg;
                if (Fujin::SaveSystem::LoadFromSlot("quicksave", g_scene, &sg)) {
                    g_editor.OnSceneReplaced();
                    g_scene.UpdateWorldTransforms();
                    if (isPlaying) {
                        g_scene.BeginPlay();    // initialize freshly loaded actors (timers etc.)
                        s_snapshot.clear();     // Stop should now restore to the loaded state
                        for (auto& actor : g_scene.GetActors()) {
                            auto* tc = actor->GetComponent<Fujin::TransformComponent>();
                            if (tc) s_snapshot[actor->GetId()] = { tc->Position, tc->Rotation, tc->Scale };
                        }
                    }
                    g_physics.Reset(g_scene);
                    OutputDebugStringA(("[SaveSystem] quicksave loaded (saveCount="
                                        + std::to_string(sg.GetInt("saveCount")) + ")\n").c_str());
                } else {
                    OutputDebugStringA("[SaveSystem] no quicksave slot to load\n");
                }
            }
            s_f5Down = f5; s_f9Down = f9;
        }

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

        // Third-person follow camera (UE5 PIE): if the GameMode's PlayerController is driving a view
        // camera, it overrides the editor's free camera during Play. World transforms were refreshed
        // this frame before/after the gameplay tick, so the spring arm reads the pawn's final position
        // with no follow lag. Falls back to the debug camera when nothing is driving one.
        if (isPlaying) {
            for (auto& actor : g_scene.GetActors()) {
                auto* pc = actor->GetComponent<Fujin::PlayerControllerComponent>();
                if (pc && pc->HasViewCamera()) {
                    pc->RefreshCamera(dt);
                    g_sceneRenderer.CameraPos    = pc->GetCameraPos();
                    g_sceneRenderer.CameraTarget = pc->GetCameraTarget();
                    break;
                }
            }
        }

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
