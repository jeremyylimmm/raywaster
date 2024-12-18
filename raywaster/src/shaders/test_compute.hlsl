RWTexture2D<float4> render_target : register(u0);

cbuffer Camera : register(b0) {
  float4x4 inv_view;
  float4x4 inv_view_proj;
};

struct Ray {
  float3 o;
  float3 d;

  float3 at(float t) {
    return o + d * t;
  }
};

struct HitRecord {
  float3 p;
  float3 n;
  float t;
  float2 uv;
};

struct Triangle {
  float3 pos[3];
  float3 norm[3];
  float2 uv[3];
};

// Yoinked from
https://stackoverflow.com/questions/42740765/intersection-between-line-and-triangle-in-3d/42752998#42752998
bool intersect_triangle(Ray R, float tmin, float tmax, Triangle tri, out HitRecord rec) { 
  float3 E1 = tri.pos[1]-tri.pos[0];
  float3 E2 = tri.pos[2]-tri.pos[0];
  float3 N = cross(E1,E2);
  float det = -dot(R.d, N);
  float invdet = 1.0f/det;
  float3 AO  = R.o - tri.pos[0];
  float3 DAO = cross(AO, R.d);
   
  float t = dot(AO,N)  * invdet; 
  float u =  dot(E2,DAO) * invdet;
  float v = -dot(E1,DAO) * invdet;

  float w = 1.0f-u-v;

  rec.p = R.at(t);
  rec.n = normalize(w * tri.norm[0] + u * tri.norm[1] + v * tri.norm[2]);
  rec.uv = w * tri.uv[0] + u * tri.uv[1] + v * tri.uv[2];
  rec.t = t;

  return (det >= 1e-6 && t > tmin && t < tmax && u >= 0.0f && v >= 0.0f && (u+v) <= 1.0f);
}
 

[numthreads(32, 32, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
  uint2 texel = thread_id.xy;

  uint w, h;
  render_target.GetDimensions(w, h);

  float2 uv = float2(texel.x, h-texel.y-1) / float2((float)w, (float)h);
  float2 ndc = uv * 2.0f - 1.0f;
  
  float4 world_space_hom = mul(inv_view_proj, float4(ndc, 0.5f, 1.0f));
  float3 world_space = world_space_hom.xyz / world_space_hom.w;

  float3 camera_pos = mul(inv_view, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;

  Ray ray;
  ray.o = camera_pos;
  ray.d = normalize(world_space - camera_pos);

  float3 color;

  Triangle tri;
  tri.pos[0] = float3(0.0f, 1.0f, 0.0f);
  tri.pos[1] = float3(-1.0f, 0.0f, 0.0f);
  tri.pos[2] = float3(1.0f, 0.0f, 0.0f);

  tri.norm[0] = float3(1.0f, 0.0f, 0.0f);
  tri.norm[1] = float3(0.0f, 1.0f, 0.0f);
  tri.norm[2] = float3(0.0f, 0.0f, 1.0f);

  tri.uv[0] = float2(0.1f, 0.0f);
  tri.uv[1] = float2(0.0f, 1.0f);
  tri.uv[2] = float2(0.0f, 0.0f);

  HitRecord rec;
  if (intersect_triangle(ray, 0.0, 10000.0f, tri, rec)) {
    color = rec.n;
  }
  else  {
    color = float3(0.01f, 0.01f, 0.01f);
  }

  if (all(texel < uint2(w, h))) {
    render_target[texel] = float4(sqrt(color), 0.0f);
  }
}