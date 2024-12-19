#include "gbuffer.hlsli"

struct PSOut {
	float4 albedo : SV_Target0;
	float4 normal : SV_Target1;
};

PSOut main(VSOut vso) 
{
	PSOut pso;
	pso.albedo = 1.0f.xxxx;
	pso.normal = float4(normalize(vso.normal) * 0.5f + 0.5f, 1.0f);

	return pso;
}