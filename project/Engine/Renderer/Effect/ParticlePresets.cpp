#include "ParticleTypes.h"

namespace Fujin {

// ---- JSON helpers ----------------------------------------------------------

static std::string RenderModeStr(EmitterRenderMode m) {
    switch (m) {
    case EmitterRenderMode::Sprite:  return "sprite";
    case EmitterRenderMode::Beam:    return "beam";
    case EmitterRenderMode::Ribbon:  return "ribbon";
    case EmitterRenderMode::Mesh:    return "mesh";
    }
    return "sprite";
}
static EmitterRenderMode RenderModeFromStr(const std::string& s) {
    if (s == "beam")   return EmitterRenderMode::Beam;
    if (s == "ribbon") return EmitterRenderMode::Ribbon;
    if (s == "mesh")   return EmitterRenderMode::Mesh;
    return EmitterRenderMode::Sprite;
}
static std::string BlendModeStr(BlendMode b) {
    return b == BlendMode::Additive ? "additive" : "alpha";
}
static BlendMode BlendModeFromStr(const std::string& s) {
    return s == "additive" ? BlendMode::Additive : BlendMode::AlphaBlend;
}

static std::string ShapeStr(EmitterShape s) {
    switch (s) {
    case EmitterShape::Sphere: return "sphere";
    case EmitterShape::Cone:   return "cone";
    case EmitterShape::Box:    return "box";
    default:                   return "point";
    }
}
static EmitterShape ShapeFromStr(const std::string& s) {
    if (s == "sphere") return EmitterShape::Sphere;
    if (s == "cone")   return EmitterShape::Cone;
    if (s == "box")    return EmitterShape::Box;
    return EmitterShape::Point;
}

static nlohmann::json Vec3J(const Vector3& v) { return { v.x, v.y, v.z }; }
static nlohmann::json Vec4J(const Vector4& v) { return { v.x, v.y, v.z, v.w }; }
static Vector3 Vec3F(const nlohmann::json& j, Vector3 def = {}) {
    if (!j.is_array() || j.size() < 3) return def;
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>() };
}
static Vector4 Vec4F(const nlohmann::json& j, Vector4 def = {}) {
    if (!j.is_array() || j.size() < 4) return def;
    return { j[0].get<float>(), j[1].get<float>(), j[2].get<float>(), j[3].get<float>() };
}

// ---- EmitterDesc serialization --------------------------------------------

void EmitterDesc::ToJson(nlohmann::json& j) const {
    j["name"]             = Name;
    j["renderMode"]       = RenderModeStr(RenderMode);
    j["simulation"]       = (Simulation == SimMode::GPU) ? "gpu" : "cpu";
    j["blendMode"]        = BlendModeStr(Blend);
    j["emissiveIntensity"]= EmissiveIntensity;
    j["maxParticles"]     = MaxParticles;
    j["loop"]        = Loop;
    j["duration"]    = Duration;
    j["localSpace"]  = LocalSpace;
    j["lightRenderer"]         = LightRenderer;
    j["lightIntensity"]        = LightIntensity;
    j["lightRange"]            = LightRange;
    j["lightRadiusScale"]      = LightRadiusScale;
    j["lightMaxCount"]         = LightMaxCount;
    j["lightUseParticleColor"] = LightUseParticleColor;
    j["lightColor"]            = Vec3J(LightColor);
    j["spriteTexture"] = SpriteTexturePath;
    j["subUVCols"]     = SubUVCols;
    j["subUVRows"]     = SubUVRows;
    j["facing"]        = (Facing == SpriteFacing::VelocityStretch) ? "stretch"
                       : (Facing == SpriteFacing::Velocity)        ? "velocity" : "camera";
    j["velStretch"]    = VelStretch;
    j["meshPath"]      = MeshPath;

    j["spawn"]["burstMode"]     = Spawn.BurstMode;
    j["spawn"]["ratePerSecond"] = Spawn.RatePerSecond;
    j["spawn"]["burstCount"]    = Spawn.BurstCount;
    j["spawn"]["spawnPerUnit"]     = Spawn.SpawnPerUnit;
    j["spawn"]["spawnPerDistance"] = Spawn.SpawnPerDistance;
    j["spawn"]["shape"]         = ShapeStr(Spawn.Shape);
    j["spawn"]["shapeRadius"]   = Spawn.ShapeRadius;
    j["spawn"]["shapeExtent"]   = Vec3J(Spawn.ShapeExtent);
    j["spawn"]["coneAngleDeg"]  = Spawn.ConeAngleDeg;
    j["spawn"]["emitDir"]       = Vec3J(Spawn.EmitDir);

    j["init"]["velMin"]       = Vec3J(Init.VelMin);
    j["init"]["velMax"]       = Vec3J(Init.VelMax);
    j["init"]["lifeMin"]      = Init.LifeMin;
    j["init"]["lifeMax"]      = Init.LifeMax;
    j["init"]["sizeMin"]      = Init.SizeMin;
    j["init"]["sizeMax"]      = Init.SizeMax;
    j["init"]["colorStart"]   = Vec4J(Init.ColorStart);
    j["init"]["colorMid"]     = Vec4J(Init.ColorMid);
    j["init"]["colorEnd"]     = Vec4J(Init.ColorEnd);
    j["init"]["useColorMid"]  = Init.UseColorMid;
    j["init"]["rotRateMin"]   = Init.RotRateMin;
    j["init"]["rotRateMax"]   = Init.RotRateMax;

    j["update"]["gravity"]           = Vec3J(Update.Gravity);
    j["update"]["drag"]              = Update.Drag;
    j["update"]["fadeColor"]         = Update.FadeColor;
    j["update"]["shrinkSize"]        = Update.ShrinkSize;
    j["update"]["sizeEndMult"]       = Update.SizeEndMult;
    j["update"]["turbulence"]        = Update.Turbulence;
    j["update"]["turbStrength"]      = Update.TurbStrength;
    j["update"]["turbFrequency"]     = Update.TurbFrequency;
    j["update"]["useAttractor"]      = Update.UseAttractor;
    j["update"]["attractorPos"]      = Vec3J(Update.AttractorPos);
    j["update"]["attractorStrength"] = Update.AttractorStrength;
    j["update"]["attractorRadius"]   = Update.AttractorRadius;
    j["update"]["useWind"]        = Update.UseWind;
    j["update"]["windVelocity"]   = Vec3J(Update.WindVelocity);
    j["update"]["windDrag"]       = Update.WindDrag;
    j["update"]["useVortex"]      = Update.UseVortex;
    j["update"]["vortexCenter"]   = Vec3J(Update.VortexCenter);
    j["update"]["vortexAxis"]     = Vec3J(Update.VortexAxis);
    j["update"]["vortexStrength"] = Update.VortexStrength;
    j["update"]["vortexInward"]   = Update.VortexInward;
    j["update"]["vortexRadius"]   = Update.VortexRadius;
    j["update"]["collision"]         = Update.Collision;
    j["update"]["restitution"]       = Update.Restitution;
    j["update"]["friction"]          = Update.Friction;
    j["update"]["collPush"]          = Update.CollPush;
    j["update"]["useSizeCurve"]      = Update.UseSizeCurve;
    j["update"]["sizeCurve"]         = Update.SizeCurve;  // array of 8

    j["beam"]["start"]      = Vec3J(Beam.Start);
    j["beam"]["end"]        = Vec3J(Beam.End);
    j["beam"]["width"]      = Beam.Width;
    j["beam"]["segments"]   = Beam.Segments;
    j["beam"]["noiseAmp"]   = Beam.NoiseAmp;
    j["beam"]["noiseSpeed"] = Beam.NoiseSpeed;
    j["beam"]["color"]      = Vec4J(Beam.Color);

    j["trail"]["uvTiling"]  = Trail.UVTiling;
    j["trail"]["uvScroll"]  = Trail.UVScroll;
}

void EmitterDesc::FromJson(const nlohmann::json& j) {
    Name              = j.value("name", "Emitter");
    RenderMode        = RenderModeFromStr(j.value("renderMode", "sprite"));
    Simulation        = (j.value("simulation", std::string("cpu")) == "gpu") ? SimMode::GPU : SimMode::CPU;
    Blend             = BlendModeFromStr(j.value("blendMode", "alpha"));
    EmissiveIntensity = j.value("emissiveIntensity", 1.0f);
    MaxParticles      = j.value("maxParticles", 200);
    Loop         = j.value("loop", true);
    Duration     = j.value("duration", 999.0f);
    LocalSpace   = j.value("localSpace", false);
    LightRenderer         = j.value("lightRenderer", false);
    LightIntensity        = j.value("lightIntensity", 2.0f);
    LightRange            = j.value("lightRange", 3.0f);
    LightRadiusScale      = j.value("lightRadiusScale", 0.0f);
    LightMaxCount         = j.value("lightMaxCount", 8);
    LightUseParticleColor = j.value("lightUseParticleColor", true);
    LightColor   = Vec3F(j.value("lightColor", nlohmann::json::array()), {1,1,1});
    SpriteTexturePath = j.value("spriteTexture", std::string());
    SubUVCols         = j.value("subUVCols", 1);
    SubUVRows         = j.value("subUVRows", 1);
    {
        std::string f = j.value("facing", std::string("camera"));
        Facing = (f == "stretch")  ? SpriteFacing::VelocityStretch
               : (f == "velocity") ? SpriteFacing::Velocity : SpriteFacing::Camera;
    }
    VelStretch        = j.value("velStretch", 0.05f);
    MeshPath          = j.value("meshPath", std::string());

    if (j.contains("spawn")) {
        auto& s = j["spawn"];
        Spawn.BurstMode     = s.value("burstMode",     false);
        Spawn.RatePerSecond = s.value("ratePerSecond", 20.0f);
        Spawn.BurstCount    = s.value("burstCount",    10);
        Spawn.SpawnPerUnit     = s.value("spawnPerUnit",     false);
        Spawn.SpawnPerDistance = s.value("spawnPerDistance", 10.0f);
        Spawn.Shape         = ShapeFromStr(s.value("shape", "point"));
        Spawn.ShapeRadius   = s.value("shapeRadius",   0.5f);
        Spawn.ShapeExtent   = Vec3F(s.value("shapeExtent", nlohmann::json::array()), {0.5f,0.5f,0.5f});
        Spawn.ConeAngleDeg  = s.value("coneAngleDeg",  30.0f);
        Spawn.EmitDir       = Vec3F(s.value("emitDir",  nlohmann::json::array()), {0,1,0});
    }
    if (j.contains("init")) {
        auto& i = j["init"];
        Init.VelMin      = Vec3F(i.value("velMin",  nlohmann::json::array()), {-1,1,-1});
        Init.VelMax      = Vec3F(i.value("velMax",  nlohmann::json::array()), { 1,3, 1});
        Init.LifeMin     = i.value("lifeMin",    1.0f);
        Init.LifeMax     = i.value("lifeMax",    2.0f);
        Init.SizeMin     = i.value("sizeMin",    0.1f);
        Init.SizeMax     = i.value("sizeMax",    0.3f);
        Init.ColorStart  = Vec4F(i.value("colorStart", nlohmann::json::array()), {1,1,1,1});
        Init.ColorMid    = Vec4F(i.value("colorMid",   nlohmann::json::array()), {1,0.5f,0,0.8f});
        Init.ColorEnd    = Vec4F(i.value("colorEnd",   nlohmann::json::array()), {1,1,1,0});
        Init.UseColorMid = i.value("useColorMid", false);
        Init.RotRateMin  = i.value("rotRateMin", -180.0f);
        Init.RotRateMax  = i.value("rotRateMax",  180.0f);
    }
    if (j.contains("update")) {
        auto& u = j["update"];
        Update.Gravity           = Vec3F(u.value("gravity", nlohmann::json::array()), {0,-9.8f,0});
        Update.Drag              = u.value("drag",              0.1f);
        Update.FadeColor         = u.value("fadeColor",         true);
        Update.ShrinkSize        = u.value("shrinkSize",        false);
        Update.SizeEndMult       = u.value("sizeEndMult",       0.0f);
        Update.Turbulence        = u.value("turbulence",        false);
        Update.TurbStrength      = u.value("turbStrength",      2.0f);
        Update.TurbFrequency     = u.value("turbFrequency",     0.5f);
        Update.UseAttractor      = u.value("useAttractor",      false);
        Update.AttractorPos      = Vec3F(u.value("attractorPos", nlohmann::json::array()), {0,0,0});
        Update.AttractorStrength = u.value("attractorStrength", 5.0f);
        Update.AttractorRadius   = u.value("attractorRadius",   10.0f);
        Update.UseWind        = u.value("useWind",        false);
        Update.WindVelocity   = Vec3F(u.value("windVelocity", nlohmann::json::array()), {1,0,0});
        Update.WindDrag       = u.value("windDrag",       1.0f);
        Update.UseVortex      = u.value("useVortex",      false);
        Update.VortexCenter   = Vec3F(u.value("vortexCenter", nlohmann::json::array()), {0,0,0});
        Update.VortexAxis     = Vec3F(u.value("vortexAxis",   nlohmann::json::array()), {0,1,0});
        Update.VortexStrength = u.value("vortexStrength", 5.0f);
        Update.VortexInward   = u.value("vortexInward",   1.0f);
        Update.VortexRadius   = u.value("vortexRadius",   8.0f);
        Update.Collision         = u.value("collision",   false);
        Update.Restitution       = u.value("restitution", 0.3f);
        Update.Friction          = u.value("friction",    0.3f);
        Update.CollPush          = u.value("collPush",    0.05f);
        Update.UseSizeCurve      = u.value("useSizeCurve", false);
        if (u.contains("sizeCurve") && u["sizeCurve"].is_array()) {
            auto& sc = u["sizeCurve"];
            for (int k = 0; k < 8 && k < (int)sc.size(); ++k) Update.SizeCurve[k] = sc[k].get<float>();
        }
    }
    if (j.contains("beam")) {
        auto& b = j["beam"];
        Beam.Start      = Vec3F(b.value("start", nlohmann::json::array()), {0,0,0});
        Beam.End        = Vec3F(b.value("end",   nlohmann::json::array()), {0,5,0});
        Beam.Width      = b.value("width",      0.08f);
        Beam.Segments   = b.value("segments",   8);
        Beam.NoiseAmp   = b.value("noiseAmp",   0.2f);
        Beam.NoiseSpeed = b.value("noiseSpeed", 3.0f);
        Beam.Color      = Vec4F(b.value("color", nlohmann::json::array()), {0.5f,0.8f,1,1});
    }
    if (j.contains("trail")) {
        auto& t = j["trail"];
        Trail.UVTiling = t.value("uvTiling", 1.0f);
        Trail.UVScroll = t.value("uvScroll", 0.0f);
    }
}

// ---- Presets ---------------------------------------------------------------

EmitterDesc MakeSteamPreset() {
    EmitterDesc d;
    d.Name         = "Steam";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.MaxParticles = 150;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 25.0f;
    d.Spawn.Shape         = EmitterShape::Point;
    d.Init.VelMin         = { -0.2f, 1.2f, -0.2f };
    d.Init.VelMax         = {  0.2f, 2.0f,  0.2f };
    d.Init.LifeMin        = 1.8f;
    d.Init.LifeMax        = 3.0f;
    d.Init.SizeMin        = 0.15f;
    d.Init.SizeMax        = 0.35f;
    d.Init.ColorStart     = { 0.85f, 0.85f, 0.85f, 0.55f };
    d.Init.ColorEnd       = { 0.60f, 0.60f, 0.60f, 0.00f };
    d.Init.RotRateMin     = -30.0f;
    d.Init.RotRateMax     =  30.0f;
    d.Update.Gravity      = { 0.0f, 0.3f, 0.0f };   // slight upward drift
    d.Update.Drag         = 0.9f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = false;
    d.Update.SizeEndMult  = 2.5f;
    return d;
}

EmitterDesc MakeSparkPreset() {
    EmitterDesc d;
    d.Name         = "Sparks";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.MaxParticles = 200;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 40.0f;
    d.Spawn.BurstCount    = 15;
    d.Spawn.Shape         = EmitterShape::Point;
    d.Init.VelMin         = { -4.0f, -1.0f, -4.0f };
    d.Init.VelMax         = {  4.0f,  8.0f,  4.0f };
    d.Init.LifeMin        = 0.3f;
    d.Init.LifeMax        = 0.9f;
    d.Init.SizeMin        = 0.04f;
    d.Init.SizeMax        = 0.10f;
    d.Init.ColorStart     = { 1.0f, 0.85f, 0.3f, 1.0f };
    d.Init.ColorEnd       = { 0.9f, 0.2f,  0.0f, 0.0f };
    d.Init.RotRateMin     = 0.0f;
    d.Init.RotRateMax     = 0.0f;
    d.Update.Gravity      = { 0.0f, -9.8f, 0.0f };
    d.Update.Drag         = 0.05f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = true;
    d.Update.SizeEndMult  = 0.0f;
    return d;
}

EmitterDesc MakeBeamPreset() {
    EmitterDesc d;
    d.Name        = "Beam";
    d.RenderMode  = EmitterRenderMode::Beam;
    d.MaxParticles = 1;
    d.Loop         = true;
    d.Beam.Start      = { 0.0f, 0.0f, 0.0f };
    d.Beam.End        = { 0.0f, 4.0f, 0.0f };
    d.Beam.Width      = 0.06f;
    d.Beam.Segments   = 10;
    d.Beam.NoiseAmp   = 0.25f;
    d.Beam.NoiseSpeed = 4.0f;
    d.Beam.Color      = { 0.5f, 0.85f, 1.0f, 0.9f };
    return d;
}

EmitterDesc MakeRibbonPreset() {
    EmitterDesc d;
    d.Name         = "Ribbon";
    d.RenderMode   = EmitterRenderMode::Ribbon;
    d.MaxParticles = 40;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 30.0f;
    d.Spawn.Shape         = EmitterShape::Point;
    d.Init.VelMin         = { -0.5f, 2.0f, -0.5f };
    d.Init.VelMax         = {  0.5f, 3.0f,  0.5f };
    d.Init.LifeMin        = 0.8f;
    d.Init.LifeMax        = 1.2f;
    d.Init.SizeMin        = 0.08f;
    d.Init.SizeMax        = 0.12f;
    d.Init.ColorStart     = { 0.6f, 0.9f, 1.0f, 0.9f };
    d.Init.ColorEnd       = { 0.2f, 0.5f, 1.0f, 0.0f };
    d.Update.Gravity      = { 0.0f, -1.0f, 0.0f };
    d.Update.Drag         = 0.3f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = true;
    d.Update.SizeEndMult  = 0.3f;
    return d;
}

EmitterDesc MakeFirePreset() {
    EmitterDesc d;
    d.Name         = "Fire";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.MaxParticles = 250;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 60.0f;
    d.Spawn.Shape         = EmitterShape::Sphere;
    d.Spawn.ShapeRadius   = 0.2f;
    d.Init.VelMin         = { -0.4f, 1.5f, -0.4f };
    d.Init.VelMax         = {  0.4f, 3.5f,  0.4f };
    d.Init.LifeMin        = 0.5f;
    d.Init.LifeMax        = 1.2f;
    d.Init.SizeMin        = 0.12f;
    d.Init.SizeMax        = 0.30f;
    d.Init.ColorStart     = { 1.0f, 0.65f, 0.1f, 1.0f };
    d.Init.ColorEnd       = { 0.6f, 0.10f, 0.0f, 0.0f };
    d.Init.RotRateMin     = -60.0f;
    d.Init.RotRateMax     =  60.0f;
    d.Update.Gravity      = { 0.0f, 1.0f, 0.0f };
    d.Update.Drag         = 0.6f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = true;
    d.Update.SizeEndMult  = 0.0f;
    return d;
}

EmitterDesc MakeExplosionPreset() {
    EmitterDesc d;
    d.Name         = "Explosion";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.MaxParticles = 300;
    d.Loop         = false;
    d.Duration     = 0.2f;
    d.Spawn.BurstMode     = true;
    d.Spawn.BurstCount    = 200;
    d.Spawn.Shape         = EmitterShape::Sphere;
    d.Spawn.ShapeRadius   = 0.4f;
    d.Init.VelMin         = { -7.0f, -7.0f, -7.0f };
    d.Init.VelMax         = {  7.0f,  7.0f,  7.0f };
    d.Init.LifeMin        = 0.4f;
    d.Init.LifeMax        = 1.2f;
    d.Init.SizeMin        = 0.10f;
    d.Init.SizeMax        = 0.30f;
    d.Init.ColorStart     = { 1.0f, 0.95f, 0.4f, 1.0f };
    d.Init.ColorMid       = { 1.0f, 0.35f, 0.0f, 0.9f };
    d.Init.UseColorMid    = true;
    d.Init.ColorEnd       = { 0.25f, 0.0f, 0.0f, 0.0f };
    d.Init.RotRateMin     = -120.0f;
    d.Init.RotRateMax     =  120.0f;
    d.Update.Gravity      = { 0.0f, -1.5f, 0.0f };
    d.Update.Drag         = 0.35f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = true;
    d.Update.SizeEndMult  = 0.0f;
    d.Update.Turbulence   = true;
    d.Update.TurbStrength = 1.5f;
    d.Update.TurbFrequency = 0.8f;
    return d;
}

EmitterDesc MakeVortexPreset() {
    EmitterDesc d;
    d.Name         = "Vortex";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.MaxParticles = 400;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 100.0f;
    d.Spawn.Shape         = EmitterShape::Sphere;
    d.Spawn.ShapeRadius   = 4.0f;
    d.Init.VelMin         = { -1.0f, 0.5f, -1.0f };
    d.Init.VelMax         = {  1.0f, 2.0f,  1.0f };
    d.Init.LifeMin        = 2.5f;
    d.Init.LifeMax        = 4.0f;
    d.Init.SizeMin        = 0.05f;
    d.Init.SizeMax        = 0.12f;
    d.Init.ColorStart     = { 0.2f, 0.6f, 1.0f, 0.9f };
    d.Init.ColorMid       = { 0.7f, 0.1f, 1.0f, 0.8f };
    d.Init.UseColorMid    = true;
    d.Init.ColorEnd       = { 0.1f, 0.05f, 0.5f, 0.0f };
    d.Init.RotRateMin     = -60.0f;
    d.Init.RotRateMax     =  60.0f;
    d.Update.Gravity      = { 0.0f, 0.0f, 0.0f };
    d.Update.Drag         = 0.05f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = false;
    d.Update.Turbulence   = true;
    d.Update.TurbStrength = 0.8f;
    d.Update.TurbFrequency = 0.3f;
    d.Update.UseAttractor  = true;
    d.Update.AttractorPos  = { 0.0f, 0.0f, 0.0f };
    d.Update.AttractorStrength = 5.0f;
    d.Update.AttractorRadius   = 8.0f;
    return d;
}

EmitterDesc MakePlasmaPreset() {
    EmitterDesc d;
    d.Name         = "Plasma";
    d.RenderMode   = EmitterRenderMode::Sprite;
    d.Simulation   = SimMode::GPU;
    d.MaxParticles = 500;
    d.Loop         = true;
    d.Spawn.RatePerSecond = 180.0f;
    d.Spawn.Shape         = EmitterShape::Sphere;
    d.Spawn.ShapeRadius   = 0.3f;
    d.Init.VelMin         = { -2.0f, 1.0f, -2.0f };
    d.Init.VelMax         = {  2.0f, 5.0f,  2.0f };
    d.Init.LifeMin        = 0.8f;
    d.Init.LifeMax        = 1.5f;
    d.Init.SizeMin        = 0.04f;
    d.Init.SizeMax        = 0.10f;
    d.Init.ColorStart     = { 0.0f, 0.8f, 1.0f, 1.0f };
    d.Init.ColorMid       = { 0.8f, 0.0f, 1.0f, 0.9f };
    d.Init.UseColorMid    = true;
    d.Init.ColorEnd       = { 1.0f, 0.2f, 0.0f, 0.0f };
    d.Init.RotRateMin     = -180.0f;
    d.Init.RotRateMax     =  180.0f;
    d.Update.Gravity      = { 0.0f, 0.5f, 0.0f };
    d.Update.Drag         = 0.2f;
    d.Update.FadeColor    = true;
    d.Update.ShrinkSize   = true;
    d.Update.SizeEndMult  = 0.0f;
    d.Update.Turbulence   = true;
    d.Update.TurbStrength = 3.0f;
    d.Update.TurbFrequency = 1.2f;
    return d;
}

} // namespace Fujin
