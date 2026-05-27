// ── Simplex 3D noise (Stefan Gustavson) ──────────────────────────────────────

static float3 _mod289_3(float3 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
static float4 _mod289_4(float4 x) { return x - floor(x * (1.0 / 289.0)) * 289.0; }
static float4 _permute(float4 x)  { return _mod289_4(((x * 34.0) + 1.0) * x); }
static float4 _taylorInvSqrt(float4 r) { return 1.79284291400159 - 0.85373472095314 * r; }

float simplex3d(float3 v)
{
    const float F3 = 1.0 / 3.0;
    const float G3 = 1.0 / 6.0;

    float3 s  = floor(v + dot(v, F3.xxx));
    float3 x  = v - s + dot(s, G3.xxx);
    float3 e  = step((float3)0, x - x.yzx);
    float3 i1 = e * (1.0 - e.zxy);
    float3 i2 = 1.0 - e.zxy * (1.0 - e);
    float3 x1 = x - i1 + G3;
    float3 x2 = x - i2 + 2.0 * G3;
    float3 x3 = x - 1.0 + 3.0 * G3;

    float4 w;
    w.x = dot(x,  x);
    w.y = dot(x1, x1);
    w.z = dot(x2, x2);
    w.w = dot(x3, x3);
    w   = max(0.6 - w, 0.0);
    float4 d4 = w * w * w * w;

    float4 perm = _permute(_permute(_permute(
          s.z + float4(0, i1.z, i2.z, 1))
        + s.y + float4(0, i1.y, i2.y, 1))
        + s.x + float4(0, i1.x, i2.x, 1));

    float4 gx = frac(perm * (1.0 / 41.0)) * 2.0 - 1.0;
    float4 gy = abs(gx) - 0.5;
    float4 gz = floor(gx + 0.5);
    gx -= gz;

    float4 norm = _taylorInvSqrt(gx*gx + gy*gy + gz*gz);
    float3 g0 = float3(gx.x, gy.x, gz.x) * norm.x;
    float3 g1 = float3(gx.y, gy.y, gz.y) * norm.y;
    float3 g2 = float3(gx.z, gy.z, gz.z) * norm.z;
    float3 g3 = float3(gx.w, gy.w, gz.w) * norm.w;

    return 42.0 * (dot(g0,x)*d4.x + dot(g1,x1)*d4.y + dot(g2,x2)*d4.z + dot(g3,x3)*d4.w);
}

// Returns a 3D simplex-noise vector (one call per component with offset seeds)
float3 Noise3(float3 p)
{
    return float3(
        simplex3d(p + float3(37.0, 17.0, 13.0)),
        simplex3d(p + float3(11.0, 47.0, 19.0)),
        simplex3d(p + float3(23.0,  7.0, 53.0)));
}

// ── Fast hash-based noise (cheaper alternative to Simplex) ───────────────────

static float _hash(float3 p)
{
    p  = frac(p * 0.3183099 + 0.1);
    p *= 17.0;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float3 Noise3_fast(float3 p)
{
    return float3(
        _hash(p + float3(37.0, 17.0, 13.0)),
        _hash(p + float3(11.0, 47.0, 19.0)),
        _hash(p + float3(23.0,  7.0, 53.0))) * 2.0 - 1.0;
}

// ── Curl noise ────────────────────────────────────────────────────────────────
// Divergence-free vector field. Good for smoke, fire, magic effects.
// Uses Simplex (quality) or hash (performance).

float3 CurlNoise(float3 p)
{
    const float e = 0.01;
    float3 dx = float3(e, 0, 0), dy = float3(0, e, 0), dz = float3(0, 0, e);

    float3 p_x0 = Noise3(p - dx), p_x1 = Noise3(p + dx);
    float3 p_y0 = Noise3(p - dy), p_y1 = Noise3(p + dy);
    float3 p_z0 = Noise3(p - dz), p_z1 = Noise3(p + dz);

    float3 curl;
    curl.x = (p_y1.z - p_y0.z) - (p_z1.y - p_z0.y);
    curl.y = (p_z1.x - p_z0.x) - (p_x1.z - p_x0.z);
    curl.z = (p_x1.y - p_x0.y) - (p_y1.x - p_y0.x);

    float len = length(curl);
    return (len > 1e-5) ? curl / len : (float3)0;
}

// Faster version using hash noise. Recommended for GPU particle update shaders.
float3 CurlNoise_fast(float3 p)
{
    const float e = 0.2;
    float3 dx = float3(e, 0, 0), dy = float3(0, e, 0), dz = float3(0, 0, e);

    float3 p_x0 = Noise3_fast(p - dx), p_x1 = Noise3_fast(p + dx);
    float3 p_y0 = Noise3_fast(p - dy), p_y1 = Noise3_fast(p + dy);
    float3 p_z0 = Noise3_fast(p - dz), p_z1 = Noise3_fast(p + dz);

    float3 curl;
    curl.x = (p_y1.z - p_y0.z) - (p_z1.y - p_z0.y);
    curl.y = (p_z1.x - p_z0.x) - (p_x1.z - p_x0.z);
    curl.z = (p_x1.y - p_x0.y) - (p_y1.x - p_y0.x);

    float len = length(curl);
    return (len > 1e-5) ? curl / len : (float3)0;
}
