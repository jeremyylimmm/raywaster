#include <span>
#include <algorithm>

#include "bvh.h"

#define ENABLE_SAH

namespace bvh {

  struct Tri {
    XMVECTOR center;
    XMVECTOR min;
    XMVECTOR max;
    uint32_t index;
  };

  template<size_t A>
  static bool sort_by_axis(const Tri& left, const Tri& right) {
    return XMVectorGetByIndex(left.center, A) < XMVectorGetByIndex(right.center, A);
  }

  static std::pair<XMVECTOR, XMVECTOR> compute_aabb(const std::span<Tri>& tris) {
    XMVECTOR min = XMVectorSplatInfinity();
    XMVECTOR max = -XMVectorSplatInfinity();

    for (auto& t : tris) {
      min = XMVectorMin(min, t.min);
      max = XMVectorMax(max, t.max);
    }

    min -= XMVectorSplatEpsilon();
    max += XMVectorSplatEpsilon();

    return {
      min, max
    };
  }

  static size_t pick_split(const std::span<Tri>& tris) {
#ifdef ENABLE_SAH
    size_t split_count = std::min(tris.size(), (size_t)10);
    size_t split_interval = tris.size()/split_count;

    size_t best = 0xffffffff;
    float best_cost = INFINITY;

    for (size_t i = 1; i < split_count; ++i) {
      size_t pos = i * split_interval;
      
      auto left = tris.subspan(0, pos);
      auto right = tris.subspan(pos);

      auto [left_min, left_max] = compute_aabb(left);
      auto [right_min, right_max] = compute_aabb(right);

      XMVECTOR left_extent = left_max-left_min;
      XMVECTOR right_extent = right_max-right_min;

      float left_surface_area = XMVectorGetX(XMVector3Dot(left_extent, XMVectorSwizzle<XM_SWIZZLE_Y, XM_SWIZZLE_Z, XM_SWIZZLE_X, XM_SWIZZLE_W>(left_extent))) * 2.0f;
      float right_surface_area = XMVectorGetX(XMVector3Dot(right_extent, XMVectorSwizzle<XM_SWIZZLE_Y, XM_SWIZZLE_Z, XM_SWIZZLE_X, XM_SWIZZLE_W>(right_extent))) * 2.0f;

      float cost = left_surface_area * float(pos) + right_surface_area * float(tris.size()-pos);

      if (cost < best_cost) {
        best_cost = cost;
        best = pos;
      }
    }

    return best;
#else
    return tris.size()/2;
#endif
  }

  static uint32_t split(std::vector<Node>& nodes, const std::span<Tri>& tris) {
    auto [min, max] = compute_aabb(tris);

    XMFLOAT3 minf3, maxf3;
    XMStoreFloat3(&minf3, min);
    XMStoreFloat3(&maxf3, max);

    if (tris.size() == 1) {
      uint32_t index = (uint32_t)nodes.size();

      nodes.push_back(Node{
        .min = minf3,
        .max = maxf3,
        .left = tris[0].index | (1 << 31), // mark as leaf
      });

      return index;
    }

    XMFLOAT3 extent;
    XMStoreFloat3(&extent, max - min);

    float max_extent = XMMax(extent.x, XMMax(extent.y, extent.z));
    int axis = max_extent == extent.x ? 0 : max_extent == extent.y ? 1 : 2;

    switch (axis) {
      case 0:
        std::sort(tris.begin(), tris.end(), sort_by_axis<0>);
        break;
      case 1:
        std::sort(tris.begin(), tris.end(), sort_by_axis<1>);
        break;
      default:
        std::sort(tris.begin(), tris.end(), sort_by_axis<2>);
        break;
    }

    size_t split_point = pick_split(tris);
    
    uint32_t left = split(nodes, std::span<Tri>(tris.begin(), tris.begin()+split_point));
    uint32_t right = split(nodes, std::span<Tri>(tris.begin()+split_point, tris.end()));

    uint32_t index = (uint32_t)nodes.size();

    nodes.push_back(Node{
      .min = minf3,
      .max = maxf3,
      .left = left,
      .right = right,
    });

    return index;
  }

  std::vector<Node> construct_bvh(const std::vector<XMFLOAT3>& positions, const std::vector<uint32_t>& indices) {
    std::vector<Tri> tris;
    tris.reserve(indices.size()/3);

    for (uint32_t i = 0; i < indices.size()/3; ++i) {
      XMVECTOR a = XMLoadFloat3(&positions[indices[i*3+0]]);
      XMVECTOR b = XMLoadFloat3(&positions[indices[i*3+1]]);
      XMVECTOR c = XMLoadFloat3(&positions[indices[i*3+2]]);

      XMVECTOR center = (a + b + c) / 3.0f;

      XMVECTOR min = XMVectorMin(a, XMVectorMin(b, c));
      XMVECTOR max = XMVectorMax(a, XMVectorMax(b, c));

      tris.push_back(Tri{
        .center = center,
        .min = min,
        .max = max,
        .index = i,
      });
    }

    std::vector<Node> nodes;
    split(nodes, std::span(tris));

    return nodes;
  }

}
