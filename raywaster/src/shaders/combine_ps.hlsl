#include "screen_quad.hlsli"
#include "common.hlsli"

cbuffer Camera : register(b0) {
  float4x4 inv_view;
  float4x4 inv_view_proj;
  float4x4 view_proj;
  uint frame;
};

Texture2D<float3> lighting_buffer : register(t0);
Texture2D<float3> hdri : register(t1);
Texture2D<float> depth_buffer : register(t2);

sampler point_clamp_sampler : register(s0);
sampler linear_clamp_sampler : register(s1);
sampler linear_wrap_sampler : register(s2);

float4 main(VSOut vso) : SV_TARGET
{
	float2 uv = vso.ndc * 0.5f + 0.5f;
	uv.y = 1.0f - uv.y;

	float3 color;

  float depth = depth_buffer.SampleLevel(point_clamp_sampler, uv, 0.0f) ;

  if (depth > 0.0f) {
		color = lighting_buffer.SampleLevel(linear_clamp_sampler, uv, 0);
  }
  else {
    float4 ndc = float4(vso.ndc, depth, 1.0f);
    float4 hom = mul(inv_view_proj, ndc);
    float3 world = hom.xyz/hom.w;

    float3 camera_pos = mul(inv_view, float4(0.0f, 0.0, 0.0f, 1.0f)).xyz;

		color = sqrt(ACESFilm(hdri.SampleLevel(linear_wrap_sampler, dir_to_equi(normalize(world-camera_pos)), 0.0f)));
  }
	
	return float4(color, 1.0f);
}