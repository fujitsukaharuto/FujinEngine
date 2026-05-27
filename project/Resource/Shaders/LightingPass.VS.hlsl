struct VSOut {
    float4 Pos : SV_POSITION;
    float2 UV  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID) {
    float2 uv  = float2((id << 1) & 2, id & 2);
    float2 pos = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    VSOut o;
    o.Pos = float4(pos, 0.0, 1.0);
    o.UV  = uv;
    return o;
}
