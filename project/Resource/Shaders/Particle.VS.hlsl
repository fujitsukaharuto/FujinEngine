cbuffer PerPass : register(b0) {
    row_major float4x4 ViewProj;
    float3 CamRight; float _p0;
    float3 CamUp;    float _p1;
};

struct VSIn {
    // Slot 0: per-vertex quad corner
    float2 LocalXY  : POSITION;
    float2 UV       : TEXCOORD0;
    // Slot 1: per-instance particle data
    float3 InstPos  : INST_POS;
    float  InstSize : INST_SIZE;
    float  InstRot  : INST_ROT;
    float3 _Pad     : INST_PAD;
    float4 InstColor: INST_COL;
};

struct VSOut {
    float4 SvPos : SV_POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR;
};

VSOut main(VSIn v) {
    float c = cos(v.InstRot);
    float s = sin(v.InstRot);
    float2 r = float2(c * v.LocalXY.x - s * v.LocalXY.y,
                      s * v.LocalXY.x + c * v.LocalXY.y);

    float3 worldPos = v.InstPos
                    + CamRight * r.x * v.InstSize
                    + CamUp    * r.y * v.InstSize;

    VSOut o;
    o.SvPos = mul(ViewProj, float4(worldPos, 1.0));
    o.UV    = v.UV;
    o.Color = v.InstColor;
    return o;
}
