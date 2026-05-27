float4 main(float4 sv : SV_POSITION, float2 uv : TEXCOORD0, float4 color : COLOR) : SV_Target0 {
    // Soft edge in V direction (width falloff)
    float edge = saturate(1.0 - abs(uv.y - 0.5) * 2.2);
    edge = pow(edge, 0.7);
    return float4(color.rgb, color.a * edge);
}
