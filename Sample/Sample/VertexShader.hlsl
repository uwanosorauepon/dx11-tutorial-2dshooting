#include "Header.hlsli"

cbuffer CBuffer : register(b0) {
    matrix viewProj;
}

PSIn main(VSIn input)
{
    PSIn output;
    output.pos = mul(float4(input.pos, 1), viewProj);
    output.uv = input.uv;
	return output;
}