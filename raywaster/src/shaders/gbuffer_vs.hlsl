#include "gbuffer.hlsli"

cbuffer Camera : register(b0) {
  float4x4 inv_view;
  float4x4 inv_view_proj;
  float4x4 view_proj;
};

StructuredBuffer<float3> positions : register(t0);
StructuredBuffer<float3> normals : register(t1);
StructuredBuffer<float2> tex_coords : register(t2);
StructuredBuffer<uint> indices : register(t3);

VSOut main(uint vertex_id : SV_VertexID)
{
  uint index = indices[vertex_id];

  float3 pos = positions[index];
  float3 normal = normals[index];
  float2 tex_coord = tex_coords[index];

  VSOut vso;
  vso.sv_pos = mul(view_proj, float4(pos, 1.0f));
  vso.normal = normal;
  vso.tex_coord = tex_coord;

  return vso;
}