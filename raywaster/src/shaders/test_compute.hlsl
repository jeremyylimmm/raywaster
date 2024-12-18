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
};

bool hit(Ray r, float tmin, float tmax, float3 center, float radius, out HitRecord rec) {
  float3 oc = center - r.o;
  float a = dot(r.d, r.d);
  float h = dot(r.d, oc);
  float c = dot(oc, oc) - radius*radius;

  float discriminant = h*h - a*c;
  if (discriminant < 0.0)
      return false;

  float sqrtd = sqrt(discriminant);

  // Find the nearest root that lies in the acceptable range.
  float root = (h - sqrtd) / a;
  if (root <= tmin || tmax <= root) {
    root = (h + sqrtd) / a;
    if (root <= tmin || tmax <= root)
        return false;
  }

  rec.t = root;
  rec.p = r.at(rec.t);
  rec.n = (rec.p - center) / radius;

  return true;
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

  float3 camera_pos = mul(inv_view, float4(0.0f, 0.0f, 0.0f, 1.0f));

  Ray ray;
  ray.o = camera_pos;
  ray.d = normalize(world_space - camera_pos);

  float3 color;

  HitRecord rec;
  if (hit(ray, 0.0, 10000.0f, float3(0.0f, 0.5f, 0.05f), 0.5f, rec)) {
    color = rec.n * 0.5 + 0.5;
  }
  else  {
    color = float3(0.01f, 0.01f, 0.01f);
  }

  if (all(texel < uint2(w, h))) {
    render_target[texel] = float4(sqrt(color), 0.0f);
  }
}