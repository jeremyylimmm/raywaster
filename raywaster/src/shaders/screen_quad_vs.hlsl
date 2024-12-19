#include "screen_quad.hlsli"

static float2 corners[] = {
  {-1.0f,  1.0f},
  { 1.0f,  1.0f},
  { 1.0f, -1.0f},
  {-1.0f, -1.0f},
};

static uint indices[] = {
  0, 1, 3, 2, 3, 1
};

VSOut main(uint vid : SV_VertexID)
{
  float4 pos = float4(corners[indices[vid]], 0.0f, 1.0f);

  VSOut vso;
  vso.sv_pos = pos;
  vso.ndc = pos.xy;

  return vso;
}