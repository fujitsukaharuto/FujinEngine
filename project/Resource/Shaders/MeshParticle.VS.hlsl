// Mesh particle: render a mesh per particle, instanced. Unlit/emissive (color-tinted), with a small
// baked directional term so the silhouette reads as a 3D shape. World = T(pos)·Ry(rot)·S(size).
cbuffer PerPass : register(b0) {
    row_major float4x4 ViewProj;
    float3 CamRight; float _p0;
    float3 CamUp;    float _p1;
};

struct VSIn {
    // Slot 0: mesh vertex (MeshVertex layout: pos, normal, tangent, uv)
    float3 LocalPos : POSITION;
    float3 Normal   : NORMAL;
    float3 Tangent  : TANGENT;
    float2 UV       : TEXCOORD0;
    // Slot 1: per-instance particle data (matches InstanceVert)
    float3 InstPos  : INST_POS;
    float  InstSize : INST_SIZE;
    float  InstRot  : INST_ROT;
    float3 _Pad     : INST_PAD;
    float4 InstColor: INST_COL;
};

struct VSOut {
    float4 SvPos   : SV_POSITION;
    float3 NormalW : NORMAL;
    float4 Color   : COLOR;
};

VSOut main(VSIn v) {
    float c = cos(v.InstRot), s = sin(v.InstRot);
    float3 lp = v.LocalPos * v.InstSize;
    float3 wp = v.InstPos + float3(c * lp.x + s * lp.z, lp.y, -s * lp.x + c * lp.z);  // spin around Y
    float3 nr = float3(c * v.Normal.x + s * v.Normal.z, v.Normal.y, -s * v.Normal.x + c * v.Normal.z);

    VSOut o;
    o.SvPos   = mul(ViewProj, float4(wp, 1.0));
    o.NormalW = nr;
    o.Color   = v.InstColor;
    return o;
}
