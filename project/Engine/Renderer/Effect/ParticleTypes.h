#pragma once
#include "Engine/Math/Math.h"
#include <string>
#include <json.hpp>

namespace Fujin {

enum class EmitterRenderMode { Sprite, Beam, Ribbon, Mesh };
enum class EmitterShape      { Point, Sphere, Cone, Box };
enum class SimMode           { CPU, GPU };
enum class BlendMode         { AlphaBlend, Additive };
// Sprite Renderer facing (Niagara "Alignment"): Camera = classic billboard; Velocity = the quad's
// long axis aligns to the particle's screen-space velocity; VelocityStretch = same, stretched along
// velocity by speed (motion streaks — sparks/rain). Velocity modes ignore per-particle Rotation.
enum class SpriteFacing      { Camera, Velocity, VelocityStretch };

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
    // Spawn-over-distance (Niagara "Spawn Per Unit"): emit as the emitter moves through the
    // world instead of over time. Spawns are placed evenly along the travelled segment so a
    // moving emitter leaves a continuous trail (pairs naturally with Ribbon render mode).
    // CPU emitters only. Mutually exclusive with BurstMode; coexists with RatePerSecond.
    bool         SpawnPerUnit     = false;
    float        SpawnPerDistance = 10.0f;  // particles per world unit travelled
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
    // Wind force (Niagara "Wind"): drag the particle's velocity toward a constant wind velocity, so
    // particles get carried by the wind (distinct from gravity's constant pull). WindDrag = how fast
    // they reach wind speed (per second). CPU + GPU.
    bool    UseWind        = false;
    Vector3 WindVelocity   = { 1.0f, 0.0f, 0.0f };
    float   WindDrag       = 1.0f;
    // Vortex force (Niagara "Vortex Velocity"): swirl tangentially around an axis line through a
    // center point, with optional inward pull. Strength = tangential accel; Inward>0 sucks toward
    // the axis (tornado), <0 pushes out. Radius = linear falloff distance (0 = no falloff). CPU + GPU.
    bool    UseVortex      = false;
    Vector3 VortexCenter   = { 0.0f, 0.0f, 0.0f };
    Vector3 VortexAxis     = { 0.0f, 1.0f, 0.0f };
    float   VortexStrength = 5.0f;
    float   VortexInward   = 1.0f;
    float   VortexRadius   = 8.0f;
    // GPU depth-buffer collision (screen-space, GPU emitters only)
    bool    Collision    = false;
    float   Restitution  = 0.3f;   // bounce
    float   Friction     = 0.3f;   // tangential damping on contact
    float   CollPush     = 0.05f;  // world push-out per contact

    // Size-over-life curve (Niagara-style): 8 multipliers applied to SizeBase across t=age/lifetime
    // (linearly interpolated). Overrides ShrinkSize when enabled. CPU + GPU.
    bool    UseSizeCurve = false;
    float   SizeCurve[8] = { 1, 1, 1, 1, 1, 1, 1, 1 };
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

// Shared Beam/Ribbon trail texturing. Uses the emitter's SpriteTexturePath as the strip texture
// (empty = plain colored strip). U runs along the length, V across the width.
struct TrailModule {
    float UVTiling = 1.0f;   // texture repeats along the length
    float UVScroll = 0.0f;   // U scroll speed (energy-flow animation), world units/sec
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

    // Local-space simulation (Niagara "Local Space"): particles are simulated relative to the
    // emitter's transform and follow it, instead of being detached into world space at spawn.
    // CPU emitters: all render modes. GPU emitters: treated as world space (not yet supported).
    bool               LocalSpace   = false;

    // Light Renderer (Niagara): emit a dynamic point light per particle so the effect actually
    // illuminates the scene. Lights are injected into the clustered lighting pass. CPU emitters
    // only (GPU particle positions aren't read back). LightMaxCount caps lights from this emitter.
    bool               LightRenderer    = false;
    float              LightIntensity   = 2.0f;
    float              LightRange       = 3.0f;
    // Radius from particle size (Niagara "Radius Scale"): 0 = use the fixed LightRange; >0 makes each
    // light's radius = particle.Size * LightRadiusScale, so the light grows/shrinks with its particle.
    float              LightRadiusScale = 0.0f;
    int                LightMaxCount    = 8;
    bool               LightUseParticleColor = true;
    Vector3            LightColor       = { 1.0f, 1.0f, 1.0f };

    // Sprite texture + SubUV flipbook (Niagara-style). Empty path = procedural soft circle.
    // SubUVCols×Rows == 1×1 means the whole texture (no flipbook); >1 plays frames over lifetime.
    std::string        SpriteTexturePath;
    int                SubUVCols = 1;
    int                SubUVRows = 1;

    // Sprite facing / alignment (CPU + GPU). VelStretch is the per-speed length multiplier used by
    // SpriteFacing::VelocityStretch (length = size * (1 + speed * VelStretch)).
    SpriteFacing       Facing    = SpriteFacing::Camera;
    float              VelStretch = 0.05f;

    // Mesh render mode: render this mesh per particle (unlit/emissive, color-tinted).
    std::string        MeshPath;

    SpawnModule  Spawn;
    InitModule   Init;
    UpdateModule Update;
    BeamModule   Beam;      // used when RenderMode == Beam
    TrailModule  Trail;     // used when RenderMode == Beam or Ribbon (texture/UV)

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
