#pragma once

#include <DirectXMath.h>

#include <variant>
#include <vector>

using namespace DirectX;

namespace bvh {
  struct Node {
    XMFLOAT3 min;
    XMFLOAT3 max;
    uint32_t left;
    uint32_t right;
  };

  std::vector<Node> construct_bvh(const std::vector<XMFLOAT3>& positions, const std::vector<uint32_t>& indices);
};

