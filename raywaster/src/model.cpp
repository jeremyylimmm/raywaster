#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

#include "model.h"

struct StructuredAttributes {
  cgltf_accessor* pos;
  cgltf_accessor* norm;
  cgltf_accessor* uv;
}; 

StructuredAttributes find_attribs(cgltf_primitive* prim) {
  StructuredAttributes result = {};

  for (int i = 0; i < prim->attributes_count; ++i) {
    cgltf_attribute* attrib = &prim->attributes[i];

    if (strcmp(attrib->name, "POSITION") == 0) {
      result.pos = attrib->data;
    }
    else if (strcmp(attrib->name, "NORMAL") == 0) {
      result.norm = attrib->data;
    }
    else if (strcmp(attrib->name, "TEXCOORD_0") == 0) {
      result.uv = attrib->data;
    }
  }

  assert(result.pos && result.norm && result.uv);

  return result;
}

template<typename T>
static T* accessor_data(cgltf_accessor* accessor) {
  uint8_t* base = (uint8_t*)accessor->buffer_view->buffer->data;
  base += accessor->buffer_view->offset + accessor->offset;
  return (T*)base;
}

std::optional<Model> load_gltf(const char* path) {
  cgltf_options options = {};
  cgltf_data* data = NULL;

  if (cgltf_parse_file(&options, path, &data) != cgltf_result_success) {
    return std::nullopt;
  }

  if (cgltf_load_buffers(&options, data, path) != cgltf_result_success) {
    cgltf_free(data);
    return std::nullopt;
  }

  std::vector<Mesh> meshes;
  std::vector<size_t> mesh_first_primitives;

  for (int mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
    cgltf_mesh* mesh = &data->meshes[mesh_index];

    mesh_first_primitives.push_back(meshes.size());

    for (int prim_index = 0; prim_index < mesh->primitives_count; ++prim_index) {
      cgltf_primitive* prim = &mesh->primitives[prim_index];
      auto [pos_acc, norm_acc, uv_acc] = find_attribs(prim);        
      
      assert(pos_acc->component_type = cgltf_component_type_r_32f);
      assert(norm_acc->component_type = cgltf_component_type_r_32f);
      assert(uv_acc->component_type = cgltf_component_type_r_32f);

      assert(pos_acc->type  == cgltf_type_vec3);
      assert(norm_acc->type == cgltf_type_vec3);
      assert(uv_acc->type   == cgltf_type_vec2);

      XMFLOAT3* pos_data = accessor_data<XMFLOAT3>(pos_acc);
      XMFLOAT3* norm_data = accessor_data<XMFLOAT3>(norm_acc);
      XMFLOAT2* uv_data = accessor_data<XMFLOAT2>(uv_acc);

      std::vector<XMFLOAT3> positions(pos_data, pos_data + pos_acc->count);
      std::vector<XMFLOAT3> normals(norm_data, norm_data + norm_acc->count);
      std::vector<XMFLOAT2> tex_coords(uv_data, uv_data + uv_acc->count);

      cgltf_accessor* ind_acc = prim->indices;
      assert(ind_acc->type == cgltf_type_scalar);

      std::vector<uint32_t> indices;

      switch (ind_acc->component_type) {
        default:
          assert(false);
          break;

        case cgltf_component_type_r_16u: {
          uint16_t* ind_data = accessor_data<uint16_t>(ind_acc);
          indices.reserve(ind_acc->count);
          for (int i = 0; i < ind_acc->count; ++i) {
            indices.push_back(ind_data[i]);
          }
        } break;

        case cgltf_component_type_r_32u: {
          uint32_t* ind_data = accessor_data<uint32_t>(ind_acc);
          indices = std::vector<uint32_t>(ind_data, ind_data + ind_acc->count);
        } break;
      }

      meshes.push_back(Mesh{
        .positions = positions,
        .normals = normals,
        .tex_coords = tex_coords,
        .indices = indices,
      });
    }
  }

  cgltf_free(data);

  return Model {
    .meshes = meshes
  };
}