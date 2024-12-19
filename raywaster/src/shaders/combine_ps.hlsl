#include "screen_quad.hlsli"

Texture2D<float3> lighting_buffer : register(t0);

sampler linear_clamp_sampler : register(s0);

float4 main(VSOut vso) : SV_TARGET
{
	float2 uv = vso.ndc * 0.5f + 0.5f;
	uv.y = 1.0f - uv.y;
	return float4(lighting_buffer.SampleLevel(linear_clamp_sampler, uv, 0.0f), 1.0f);
}