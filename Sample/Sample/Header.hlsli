struct VSIn {
    float3 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct PSIn {
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};
