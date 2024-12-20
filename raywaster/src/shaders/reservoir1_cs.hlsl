#include "common.hlsli"

cbuffer Constants : register(b0) {
  uint width;
  uint height;
  uint frame;
};

RWStructuredBuffer<SerializedReservoir> reservoir_buffer : register(u0);

Texture2D gbuffer_normal : register(t0);
Texture2D<float3> hdri : register(t1);

SamplerState point_clamp_sampler : register(s0);
SamplerState linear_wrap_sampler : register(s1);

[numthreads(16, 16, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
  uint2 texel = thread_id.xy;
  uint state = hash32(texel.y * width + texel.x) ^ hash32(frame);

  float2 uv = float2(texel)/float2(width,height);

  const int m = 4;

  float wsum = 0.0f;
  float3 y = 0.0f;
  float py = 0.0f;

  float3 normal = gbuffer_normal.SampleLevel(point_clamp_sampler, uv, 1.0f).xyz * 2.0f - 1.0f;

  for (int i = 0; i < m; ++i) {
    float3 x = sample_cosine_hemisphere(state, normal);
    float s = dot(normal, x)/PI;
    float r = compute_luminance(hdri.SampleLevel(linear_wrap_sampler, dir_to_equi(x), 0.0f));

    float w = r/s;
    wsum += w;

    if (uniform_random(state) < w / wsum) {
      y = x;
      py = r;
    }
  }

  if (texel.x < width && texel.y < height) {
    float3 dir = y * 0.5f + 0.5f;
    uint3 worded = uint3(dir * 255.0f) & 0xff;

    SerializedReservoir result;
    result.y = (worded.x << 16) | (worded.y << 8) | (worded.z);
    result.w = wsum;
    result.factor = 1.0f/py*(1.0f/float(m)*wsum);

    reservoir_buffer[thread_id.y*width+thread_id.x] = result;
  }
}