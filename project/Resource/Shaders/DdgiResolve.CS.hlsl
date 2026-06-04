// DDGI resolve — turn the per-probe accumulated sums into an SH projection and temporally blend it
// into the probe buffer, then zero the accumulator for next frame.
//   • L_k ≈ (4π / count) · Σ L·Y_k  (Monte-Carlo SH projection over the injected samples).
//   • Probes blend toward the new estimate by BlendRate per frame, so flicker from the noisy / moving
//     screen-space source is smoothed and the volume reacts to lighting changes over a few frames.
//   • Probes that received no samples this frame are left unchanged (no decay) for stability.
static const float SH_SCALE = 64.0;
static const float FOUR_PI  = 12.56637061;

cbuffer DdgiResolveCB : register(b0) {
    uint  NumProbes;
    float BlendRate;     // fraction of the new estimate taken per frame (e.g. 0.1)
    float2 _p0;
};

RWStructuredBuffer<int>     Accum  : register(u0);   // [probe*28 + i]
struct ProbeSH { float sh[27]; float pad; };
RWStructuredBuffer<ProbeSH> Probes : register(u1);

[numthreads(64, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    uint pi = DTid.x;
    if (pi >= NumProbes) return;

    uint abase = pi * 28u;
    int  count = Accum[abase + 27u];

    // The (4π/count) Monte-Carlo projection over-amplifies low-sample probes (a couple of clustered
    // directions treated as the whole sphere → blown-out white). Dividing by max(count, MIN) instead
    // caps that amplification (errs dim, never bright) while still letting every sampled probe update —
    // important because the half-res injection gives each probe only ~1/4 the samples per frame.
    if (count > 0) {
        float factor = (FOUR_PI / max((float)count, 8.0)) / SH_SCALE;
        [unroll]
        for (int i = 0; i < 27; ++i) {
            float newC = (float)Accum[abase + (uint)i] * factor;
            Probes[pi].sh[i] = lerp(Probes[pi].sh[i], newC, BlendRate);
        }
    }

    // Reset the accumulator for the next frame's injection.
    [unroll]
    for (int j = 0; j < 28; ++j) Accum[abase + (uint)j] = 0;
}
