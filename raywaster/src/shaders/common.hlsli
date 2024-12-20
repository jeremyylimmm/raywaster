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

