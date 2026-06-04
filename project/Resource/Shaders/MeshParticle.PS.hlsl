struct PSIn {
    float4 SvPos   : SV_POSITION;
    float3 NormalW : NORMAL;
    float4 Color   : COLOR;
};

float4 main(PSIn i) : SV_Target0 {
    // Unlit/emissive, with a soft hemispheric term so the mesh shape is readable.
    float3 L   = normalize(float3(0.35, 0.9, 0.35));
    float  ndl = saturate(dot(normalize(i.NormalW), L)) * 0.6 + 0.4;
    return float4(i.Color.rgb * ndl, i.Color.a);
}
