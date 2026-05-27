float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 {
    // Procedural soft-circle sprite (no texture required)
    float2 c = uv - 0.5;
    float  r = dot(c, c) * 4.0;     // 0 at center, 1 at edge
    float  a = saturate(1.0 - r);
    a = pow(a, 0.6);                 // softer falloff
    return float4(color.rgb, color.a * a);
}
