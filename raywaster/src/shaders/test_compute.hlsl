RWTexture2D<float4> render_target : register(u0);

[numthreads(32, 32, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
  uint w, h;
  render_target.GetDimensions(w, h);

  if (all(thread_id.xy < uint2(w, h))) {
    render_target[thread_id.xy] = float4(0.0f, 0.0f, 1.0f, 0.0f);
  }
}