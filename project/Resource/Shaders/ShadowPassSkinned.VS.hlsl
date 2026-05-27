cbuffer PerObject : register(b0) {
    row_major float4x4 LightWVP;
};

cbuffer BonePalette : register(b1) {
    row_major float4x4 Bones[128];
};

float4 main(
    float3 Position    : POSITION,
    uint4  BoneIndices : BLENDINDICES,
    float4 BoneWeights : BLENDWEIGHT
) : SV_POSITION {
    float4x4 skin =
        BoneWeights.x * Bones[BoneIndices.x] +
        BoneWeights.y * Bones[BoneIndices.y] +
        BoneWeights.z * Bones[BoneIndices.z] +
        BoneWeights.w * Bones[BoneIndices.w];
    float4 skinnedPos = mul(skin, float4(Position, 1.0));
    return mul(LightWVP, skinnedPos);
}
