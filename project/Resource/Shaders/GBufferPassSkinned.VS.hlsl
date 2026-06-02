cbuffer PerObject : register(b0) {
    row_major float4x4 WorldViewProj;       // current (jittered) world-view-proj
    row_major float4x4 World;
    row_major float4x4 PrevWorldViewProj;   // previous frame's (jittered) world-view-proj
};

cbuffer BonePalette : register(b1) {
    row_major float4x4 Bones[128];
};

cbuffer PrevBonePalette : register(b3) {
    row_major float4x4 PrevBones[128];      // previous frame's bone palette (motion vectors)
};

struct VSIn {
    float3 Pos         : POSITION;
    float3 Normal      : NORMAL;
    float3 Tangent     : TANGENT;
    float2 UV          : TEXCOORD0;
    uint4  BoneIndices : BLENDINDICES;
    float4 BoneWeights : BLENDWEIGHT;
};

struct VSOut {
    float4 Sv      : SV_POSITION;
    float3 WNorm   : NORMAL;
    float3 WTangent: TANGENT;
    float2 UV      : TEXCOORD0;
    float4 CurClip : TEXCOORD1;   // current clip pos (for motion vectors)
    float4 PrevClip: TEXCOORD2;   // previous clip pos (prev pose + prev wvp)
};

VSOut main(VSIn v) {
    float4x4 skin =
        v.BoneWeights.x * Bones[v.BoneIndices.x] +
        v.BoneWeights.y * Bones[v.BoneIndices.y] +
        v.BoneWeights.z * Bones[v.BoneIndices.z] +
        v.BoneWeights.w * Bones[v.BoneIndices.w];

    float3 skinnedPos    = mul(skin, float4(v.Pos, 1.0)).xyz;
    float3 skinnedNormal = normalize(mul((float3x3)skin, v.Normal));
    float3 rawT          = mul((float3x3)skin, v.Tangent);
    float3 skinnedTangent = (dot(rawT, rawT) > 1e-6f) ? normalize(rawT) : float3(0.0, 0.0, 0.0);

    // Previous-frame skinned position: last frame's pose × last frame's world-view-proj.
    float4x4 prevSkin =
        v.BoneWeights.x * PrevBones[v.BoneIndices.x] +
        v.BoneWeights.y * PrevBones[v.BoneIndices.y] +
        v.BoneWeights.z * PrevBones[v.BoneIndices.z] +
        v.BoneWeights.w * PrevBones[v.BoneIndices.w];
    float3 prevSkinnedPos = mul(prevSkin, float4(v.Pos, 1.0)).xyz;

    VSOut o;
    o.Sv      = mul(WorldViewProj, float4(skinnedPos, 1.0));
    o.CurClip = o.Sv;
    o.PrevClip= mul(PrevWorldViewProj, float4(prevSkinnedPos, 1.0));
    o.WNorm   = normalize(mul((float3x3)World, skinnedNormal));
    float3 wRawT = mul((float3x3)World, skinnedTangent);
    o.WTangent = (dot(wRawT, wRawT) > 1e-6f) ? normalize(wRawT) : float3(0.0, 0.0, 0.0);
    o.UV      = v.UV;
    return o;
}
