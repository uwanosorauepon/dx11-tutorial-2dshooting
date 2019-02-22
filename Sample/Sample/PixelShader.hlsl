#include "Header.hlsli"

Texture2D _texture : register(t0);
SamplerState _sampler : register(s0);

cbuffer CBuffer : register(b0)
{
    float4 color;
}

float4 main(PSIn input) : SV_TARGET
{
    return _texture.Sample(_sampler, input.uv) * color;
}