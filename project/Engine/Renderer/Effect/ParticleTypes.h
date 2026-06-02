#pragma once
#include "Engine/Math/Math.h"
#include <string>
#include <json.hpp>

namespace Fujin {

enum class EmitterRenderMode { Sprite, Beam, Ribbon };
enum class EmitterShape      { Point, Sphere, Cone, Box };
enum class SimMode           { CPU, GPU };
enum class BlendMode         { AlphaBlend, Additive };

// ---- Particle runtime state ------------------------------------------------

struct Particle {
    Vector3 Position;
    Vector3 Velocity;
    Vector4 Color;
    float   SizeBase   = 0.1f;
    float   Size       = 0.1f;
    float   Rotation   = 0.0f;
    float   RotRate    = 0.0f;
    float   Age        = 0.0f;
    float   Lifetime   = 1.0f;
    bool    Active     = false;
};

// ---- Module data structs (plain serializable data) -------------------------

struct SpawnModule {
    bool         BurstMode     = false;     // true=burst only, false=continuous rate
    float        RatePerSecond = 20.0f;
    int          BurstCount    = 10;        // particles spawned per burst
    EmitterShape Shape         = EmitterShape::Point;
    float        ShapeRadius   = 0.5f;
    Vector3      ShapeExtent   = { 0.5f, 0.5f, 0.5f };
    float        ConeAngleDeg  = 30.0f;
    Vector3      EmitDir       = { 0.0f, 1.0f, 0.0f };
};

struct InitModule {
    Vector3 VelMin      = { -1.0f,  1.0f, -1.0f };
    Vector3 VelMax      = {  1.0f,  3.0f,  1.0f };
    float   LifeMin     = 1.0f;
    float   LifeMax     = 2.0f;
    float   SizeMin     = 0.1f;
    float   SizeMax     = 0.3f;
    Vector4 ColorStart  = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 ColorMid    = { 1.0f, 0.5f, 0.0f, 0.8f };
    Vector4 ColorEnd    = { 1.0f, 1.0f, 1.0f, 0.0f };
    bool    UseColorMid = false;
    float   RotRateMin  = -180.0f;
    float   RotRateMax  =  180.0f;
};

struct UpdateModule {
    Vector3 Gravity     = { 0.0f, -9.8f, 0.0f };
    float   Drag        = 0.1f;
    bool    FadeColor   = true;
    bool    ShrinkSize  = false;
    float   SizeEndMult = 0.0f;
    // Turbulence
    bool    Turbulence      = false;
    float   TurbStrength    = 2.0f;
    float   TurbFrequency   = 0.5f;
    // Point Attractor (positive strength = attract, negative = repel)
    bool    UseAttractor        = false;
    Vector3 AttractorPos        = { 0.0f, 0.0f, 0.0f };
    float   AttractorStrength   = 5.0f;
    float   AttractorRadius     = 10.0f;
};

struct BeamModule {
    Vector3 Start      = { 0.0f, 0.0f, 0.0f };
    Vector3 End        = { 0.0f, 5.0f, 0.0f };
    float   Width      = 0.08f;
    int     Segments   = 8;
    float   NoiseAmp   = 0.2f;    // lateral noise amplitude
    float   NoiseSpeed = 3.0f;    // noise animation speed
    Vector4 Color      = { 0.5f, 0.8f, 1.0f, 1.0f };
};

// ---- GPU spawn data (CPU→GPU upload, matches HLSL GPUSpawnData, 80 bytes) --

struct GPUSpawnData {
    float pos[3];    float lifetime;
    float vel[3];    float sizeBase;
    float colorStart[4];
    float colorEnd[4];
    float rot;       float rotRate;  float pad[2];
};
static_assert(sizeof(GPUSpawnData) == 80, "GPUSpawnData size mismatch");

// ---- GPU particle runtime state (matches HLSL GPUParticle, 96 bytes) -------

struct GPUParticleLayout {
    float pos[3];    float age;
    float vel[3];    float lifetime;
    float colorStart[4];
    float colorEnd[4];
    float color[4];
    float sizeBase;  float size;
    float rot;       float rotRate;
};
static_assert(sizeof(GPUParticleLayout) == 96, "GPUParticleLayout size mismatch");

// ---- Emitter descriptor (serializable) ------------------------------------

struct EmitterDesc {
    std::string        Name             = "NewEmitter";
    EmitterRenderMode  RenderMode        = EmitterRenderMode::Sprite;
    SimMode            Simulation        = SimMode::CPU;
    BlendMode          Blend             = BlendMode::AlphaBlend;
    float              EmissiveIntensity = 1.0f;  // HDR color multiplier; >1 triggers bloom
    int                MaxParticles = 200;
    bool               Loop         = true;
    float              Duration     = 999.0f;  // spawn stop time when Loop=false

    SpawnModule  Spawn;
    InitModule   Init;
    UpdateModule Update;
    BeamModule   Beam;      // used when RenderMode == Beam

    void ToJson(nlohmann::json& j) const;
    void FromJson(const nlohmann::json& j);
};

// ---- Preset factories ------------------------------------------------------

EmitterDesc MakeSteamPreset();
EmitterDesc MakeSparkPreset();
EmitterDesc MakeBeamPreset();
EmitterDesc MakeRibbonPreset();
EmitterDesc MakeFirePreset();
EmitterDesc MakeExplosionPreset();
EmitterDesc MakeVortexPreset();
EmitterDesc MakePlasmaPreset();

} // namespace Fujin
