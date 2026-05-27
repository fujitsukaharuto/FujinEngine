// ── Hash-based stateless random (position → random value) ────────────────────
// Useful for stateless emitters and procedural textures.
// Warning: sin-based hashes degrade at very large input values (world-space).

float rand3dTo1d(float3 value, float3 dotDir = float3(12.9898, 78.233, 37.719))
{
    float3 smallValue = sin(value);
    float  random     = dot(smallValue, dotDir);
    return frac(sin(random) * 143758.5453);
}

float rand2dTo1d(float2 value, float2 dotDir = float2(12.9898, 78.233))
{
    float2 smallValue = sin(value);
    float  random     = dot(smallValue, dotDir);
    return frac(sin(random) * 143758.5453);
}

float rand1dTo1d(float value, float mutator = 0.546)
{
    return frac(sin(value + mutator) * 143758.5453);
}

// ── To-2d helpers ─────────────────────────────────────────────────────────────

float2 rand3dTo2d(float3 value)
{
    return float2(
        rand3dTo1d(value, float3(12.989, 78.233, 37.719)),
        rand3dTo1d(value, float3(39.346, 11.135, 83.155)));
}

float2 rand2dTo2d(float2 value)
{
    return float2(
        rand2dTo1d(value, float2(12.989, 78.233)),
        rand2dTo1d(value, float2(39.346, 11.135)));
}

float2 rand1dTo2d(float value)
{
    return float2(
        rand1dTo1d(value, 3.9812),
        rand1dTo1d(value, 7.1536));
}

// ── To-3d helpers ─────────────────────────────────────────────────────────────

float3 rand3dTo3d(float3 value)
{
    return float3(
        rand3dTo1d(value, float3(12.989, 78.233, 37.719)),
        rand3dTo1d(value, float3(39.346, 11.135, 83.155)),
        rand3dTo1d(value, float3(73.156, 52.235,  9.151)));
}

float3 rand2dTo3d(float2 value)
{
    return float3(
        rand2dTo1d(value, float2(12.989, 78.233)),
        rand2dTo1d(value, float2(39.346, 11.135)),
        rand2dTo1d(value, float2(73.156, 52.235)));
}

float3 rand1dTo3d(float value)
{
    return float3(
        rand1dTo1d(value, 3.9812),
        rand1dTo1d(value, 7.1536),
        rand1dTo1d(value, 5.7241));
}

// ── XorShift32 stateful PRNG ──────────────────────────────────────────────────
// Use one instance per thread. Call InitSeed once, then Generate* as needed.
// Period: 2^32 - 1. Suitable for GPU particle spawning / update compute shaders.

class RandomGenerator
{
    uint seed;

    void InitSeed(uint3 baseSeed, float perTime)
    {
        seed  = baseSeed.x * 73856093u ^ baseSeed.y * 19349663u ^ baseSeed.z * 83492791u;
        seed ^= asuint(perTime * 1000.0f);
    }

    uint XorShift()
    {
        seed ^= (seed << 13);
        seed ^= (seed >> 17);
        seed ^= (seed <<  5);
        return seed;
    }

    // Returns uniform [0, 1)
    float Generate1d()
    {
        return float(XorShift() & 0x00FFFFFFu) / 16777216.0f;
    }

    // Returns uniform [-1, 1] per component
    float3 Generate3d()
    {
        return float3(Generate1d(), Generate1d(), Generate1d()) * 2.0f - 1.0f;
    }

    // Returns uniform direction on the unit sphere
    float3 GenerateUnitSphereDirection()
    {
        float u     = Generate1d();
        float v     = Generate1d();
        float theta = 6.28318530f * u;
        float phi   = acos(2.0f * v - 1.0f);
        return float3(sin(phi) * cos(theta),
                      sin(phi) * sin(theta),
                      cos(phi));
    }

    // Returns uniform [minValue, maxValue)
    float GenerateRange1d(float minValue, float maxValue)
    {
        return lerp(minValue, maxValue, Generate1d());
    }
};
