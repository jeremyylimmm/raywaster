#include "gbuffer.hlsli"

float4 main(VSOut vso) : SV_TARGET
{
	return float4(sqrt(normalize(vso.normal) * 0.5f + 0.5f), 1.0f);
}