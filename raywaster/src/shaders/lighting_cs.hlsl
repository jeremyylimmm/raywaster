#define MAX_BVH_DEPTH 32
#define PI 3.14159265f

struct BVHNode {
  float3 min;
  float3 max;
  uint children[2];
};

RWTexture2D<float4> render_target : register(u0);

cbuffer Camera : register(b0) {
  float4x4 inv_view;
  float4x4 inv_view_proj;
  float4x4 view_proj;
};

StructuredBuffer<float3> positions : register(t0);
StructuredBuffer<float3> normals : register(t1);
StructuredBuffer<float2> tex_coords : register(t2);
StructuredBuffer<uint> indices : register(t3);
StructuredBuffer<BVHNode> bvh : register(t4);

Texture2D hdri : register(t5);
Texture2D<float> depth_buffer : register(t6);
Texture2D gbuffer_albedo : register(t7);
Texture2D gbuffer_normal : register(t8);

SamplerState linear_wrap_sampler : register(s0);
SamplerState point_clamp_sampler : register(s1);

struct Ray {
  float3 o;
  float3 d;
  float3 inv_d;

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

// Yoinked from
//https://stackoverflow.com/questions/42740765/intersection-between-line-and-triangle-in-3d/42752998#42752998
bool intersect_triangle(Ray r, float tmin, float tmax, uint tri_idx, out HitRecord rec) { 
  uint i0 = indices[tri_idx*3+0];
  uint i1 = indices[tri_idx*3+2];
  uint i2 = indices[tri_idx*3+1];

  float3 E1 = positions[i1]-positions[i0];
  float3 E2 = positions[i2]-positions[i0];
  float3 N = cross(E1,E2);
  float det = -dot(r.d, N);
  float invdet = 1.0f/det;
  float3 AO  = r.o - positions[i0];
  float3 DAO = cross(AO, r.d);
   
  float t = dot(AO,N)  * invdet; 
  float u =  dot(E2,DAO) * invdet;
  float v = -dot(E1,DAO) * invdet;

  float w = 1.0f-u-v;

  rec.p = r.at(t);
  rec.n = normalize(w * normals[i0] + u * normals[i1] + v * normals[i2]);
  rec.uv = w * tex_coords[i0] + u * tex_coords[i1] + v * tex_coords[i2];
  rec.t = t;

  return (det >= 1e-6 && t > tmin && t < tmax && u >= 0.0f && v >= 0.0f && (u+v) <= 1.0f);
}

float ray_aabb_dst(Ray ray, float3 boxMin, float3 boxMax)
{
  float3 tMin = (boxMin - ray.o) * ray.inv_d;
  float3 tMax = (boxMax - ray.o) * ray.inv_d;
  float3 t1 = min(tMin, tMax);
  float3 t2 = max(tMin, tMax);
  float tNear = max(max(t1.x, t1.y), t1.z);
  float tFar = min(min(t2.x, t2.y), t2.z);

  bool hit = tFar >= tNear && tFar > 0;
  float dst = hit ? tNear > 0 ? tNear : 0 : 1.#INF;

  return dst;
};

bool hit_scene(Ray ray, out HitRecord rec, out uint box_test_count) {
  uint bvh_count, bvh_stride;
  bvh.GetDimensions(bvh_count, bvh_stride);

  int stack_count = 0;
  uint stack[MAX_BVH_DEPTH];

  stack[stack_count++] = bvh_count-1;

  float closest = 100000.0f;
  bool hit = false;

  box_test_count = 0;

  while (stack_count) {
    BVHNode node = bvh[stack[--stack_count]];

    if (node.children[0] >> 31) {
      HitRecord temp;
      if (intersect_triangle(ray, 0.0, closest, node.children[0] & ~(1 << 31), temp)) {
        closest = temp.t;
        rec = temp;
        hit = true;
      }
    }
    else{
      box_test_count++;

      float dists[2];
      bool hits[2];

      for (int i = 0; i < 2; ++i) {
        dists[i] = ray_aabb_dst(ray, bvh[node.children[i]].min, bvh[node.children[i]].max);
        hits[i] = dists[i] < closest;
      }

      uint closer_child = dists[0] < dists[1] ? 0 : 1;
      uint further_child = (closer_child + 1) % 2;

      if (hits[further_child] && stack_count < MAX_BVH_DEPTH) {
        stack[stack_count++] = node.children[further_child];
      }

      if (hits[closer_child] && stack_count < MAX_BVH_DEPTH) {
        stack[stack_count++] = node.children[closer_child];
      }
    }
  }

  return hit;
}

float2 dir_to_equi(float3 d) {
  float x = (atan2(d.x, d.z) * 1.0f/PI) * 0.5f + 0.5f;
  float y = (asin(d.y) * 2.0f/PI) * 0.5f + 0.5f;
  return float2(x, 1.0f-y);
}

Ray make_ray(float3 o, float3 d) {
  Ray ray;
  ray.o = o;
  ray.d = d;
  ray.inv_d = 1.0f / ray.d;
  return ray;
}

float3 frag_func(float depth, float3 normal, float3 world, float3 camera_pos) {
  float3 view_dir = normalize(camera_pos - world);

  if (depth < 1e-6) {
    float2 uv = dir_to_equi(-view_dir);
    return hdri.SampleLevel(linear_wrap_sampler, uv, 0).rgb;
  }

  float3 light_dir = normalize(float3(1.0f, 1.0f, 1.0f));

  Ray ray = make_ray(world + normal * 1e-6, light_dir);

  HitRecord rec;
  uint box_test_count;

  if (hit_scene(ray, rec, box_test_count)) {
    return 0.0f.rrr;
  }
  else{
    return max(dot(normal, light_dir), 0.0f) * 1.0f.rrr;
  }
}

[numthreads(16, 16, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
  uint2 texel = thread_id.xy;

  uint w, h;
  render_target.GetDimensions(w, h);

  float2 screen_uv = float2(texel.x, texel.y) / float2((float)w, (float)h);

  float3 camera_pos = mul(inv_view, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
  float depth = depth_buffer.Sample(point_clamp_sampler, screen_uv, 2);
  float3 normal = gbuffer_normal.SampleLevel(point_clamp_sampler, screen_uv, 2).xyz * 2.0f - 1.0f;

  float4 ndc = float4(screen_uv.x * 2.0f - 1.0, screen_uv.y * -2.0f + 1.0f, depth, 1.0f);
  float4 hom = mul(inv_view_proj, ndc);
  float3 world = hom.xyz / hom.w;

  float3 color = normal * 0.5f + 0.5f;//frag_func(depth, normal, world, camera_pos);

  if (all(texel < uint2(w, h))) {
    render_target[texel] = float4(sqrt(color), 0.0f);
  }
}