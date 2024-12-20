#define PI 3.14159265f

float3 ACESFilm(float3 x)
{
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;
  return saturate((x*(a*x+b))/(x*(c*x+d)+e));
}

float2 dir_to_equi(float3 d) {
  float x = (atan2(d.x, d.z) * 1.0f/PI) * 0.5f + 0.5f;
  float y = (asin(d.y) * 2.0f/PI) * 0.5f + 0.5f;
  return float2(x, 1.0f-y);
}

uint hash32(uint key)
{
  key = ~key + (key << 15); // key = (key << 15) - key - 1;
  key = key ^ (key >> 12);
  key = key + (key << 2);
  key = key ^ (key >> 4);
  key = key * 2057; // key = (key + (key << 3)) + (key << 11);
  key = key ^ (key >> 16);
  return key;
}

uint splitmix32(inout uint state) {
  uint z = (state += 0x9e3779b9);
  z ^= z >> 16; z *= 0x21f0aaad;
  z ^= z >> 15; z *= 0x735a2d97;
  z ^= z >> 15;
  return z;
}

float uniform_random(inout uint state) {
  return (float)((double)splitmix32(state)/(double)0xffffffff);
}

float3 random_cosine_direction(inout uint state) {
  float r1 = uniform_random(state);
  float r2 = uniform_random(state);

  float phi = 2.0f * PI * r1;
  float x = cos(phi) * sqrt(r2);
  float y = sin(phi) * sqrt(r2);
  float z = sqrt(1.0f-r2);

  return float3(x, y, z);
}

float3 sample_cosine_hemisphere(inout uint state, float3 n) {
  float3 a;

  if (abs(n.x) > 0.9f) {
    a = float3(0.0, 1.0, 0.0);
  } else {       
    a = float3(1.0, 0.0, 0.0);
  };

  float3 s = normalize(cross(n, a));
  float3 t = cross(n, s);

  float3 v = random_cosine_direction(state);
  return v.x*s + v.y*t + v.z*n;
}

float3 sample_uniform_sphere(inout uint state) {
  float u = uniform_random(state);
  float v = uniform_random(state);

  float z = 1.0f - 2.0f * u;
  float r = sqrt(1.0f - z*z);
  float phi = 2 * PI * v;

  return normalize(float3(r * cos(phi), r * sin(phi), z));
}

float3 sample_uniform_hemisphere(inout uint state, float3 n) {
  float3 v = sample_uniform_sphere(state);
  if (dot(v, n) < 0.0f) {
    v *= -1.0f;
  }
  return v;
}

float compute_luminance(float3 col) {
  return 0.2126f * col.r + 0.7152f * col.g + 0.0722f * col.b;
}

struct SerializedReservoir {
  uint y;
  float w;
  float factor;

  float3 dir() {
    uint3 result;
    result.x = (y >> 16) & 0xff;
    result.y = (y >> 8) & 0xff;
    result.z = (y) & 0xff;
    return (float3(result) / 255.0f) * 2.0f - 1.0f;
  }
};
