#pragma once

#include <DirectXMath.h>
using namespace DirectX;

#include <vector>
#include <optional>

struct Mesh {
  std::vector<XMFLOAT3> positions;
  std::vector<XMFLOAT3> normals;
  std::vector<XMFLOAT2> tex_coords;
  std::vector<uint32_t> indices;
};

struct Instance {
  size_t mesh;
  XMMATRIX transform;
};

struct Model {
  std::vector<Mesh> meshes;
  std::vector<Instance> instances;
};

std::optional<Model> load_gltf(const char* path);
