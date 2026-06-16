cbuffer PerPass : register(b0) {
    row_major float4x4 ViewProj;
    float3 CamRight; float _p0;
    float3 CamUp;    float _p1;
};

// Per-emitter (root constants): flipbook grid + texture flag + sprite facing.
cbuffer SubUVCB : register(b1) {
    int SubUVCols; int SubUVRows; int HasTexture; int Facing;   // Facing: 0=Camera 1=Velocity 2=Stretch
    float VelStretch; int _su1; int _su2; int _su3;
};

// Remap a [0,1] quad uv into the current flipbook frame's sub-rect based on age fraction.
float2 SubUV(float2 uv, float ageFrac) {
    int cols = max(SubUVCols, 1);
    int rows = max(SubUVRows, 1);
    int total = cols * rows;
    if (total <= 1) return uv;
    int frame = (int)floor(saturate(ageFrac) * total);
    frame = min(frame, total - 1);
    int col = frame % cols;
    int row = frame / cols;
    return (float2(col, row) + uv) / float2(cols, rows);
}

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
    float3 InstVel  : INST_VEL;
};

struct VSOut {
    float4 SvPos : SV_POSITION;
    float2 UV    : TEXCOORD0;
    float4 Color : COLOR;
};

VSOut main(VSIn v) {
    // Velocity in the camera (screen) plane, used by the velocity facing modes.
    float2 velS = float2(dot(v.InstVel, CamRight), dot(v.InstVel, CamUp));
    float  slen = length(velS);

    float2 off;   // quad offset in the (CamRight, CamUp) basis, in size units
    if (Facing > 0 && slen > 1e-4) {
        // Align the quad's Y axis to the screen-space velocity; X is perpendicular. Stretch the
        // length axis by speed for the Stretch mode (motion streaks). Per-particle rotation is
        // intentionally ignored here (Niagara velocity alignment overrides rotation).
        float2 vdir  = velS / slen;
        float2 vperp = float2(-vdir.y, vdir.x);
        float  lenScale = (Facing == 2) ? (1.0 + length(v.InstVel) * VelStretch) : 1.0;
        off = v.LocalXY.x * vperp + v.LocalXY.y * vdir * lenScale;
    } else {
        // Camera-facing billboard with per-particle rotation.
        float c = cos(v.InstRot);
        float s = sin(v.InstRot);
        off = float2(c * v.LocalXY.x - s * v.LocalXY.y,
                     s * v.LocalXY.x + c * v.LocalXY.y);
    }

    float3 worldPos = v.InstPos
                    + CamRight * off.x * v.InstSize
                    + CamUp    * off.y * v.InstSize;

    VSOut o;
    o.SvPos = mul(ViewProj, float4(worldPos, 1.0));
    o.UV    = SubUV(v.UV, v._Pad.x);   // _Pad.x carries age/lifetime
    o.Color = v.InstColor;
    return o;
}
