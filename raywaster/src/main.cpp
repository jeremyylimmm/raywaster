#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <algorithm>

#include <iostream>
#include <utility>
#include <optional>
#include <format>
#include <vector>
#include <fstream>
#include <chrono>

#include "model.h"
#include "bvh.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

using namespace DirectX;

static constexpr DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

static struct {
  bool closed;
  std::optional<std::pair<int32_t, int32_t>> resize;
  bool resized;
  XMFLOAT2 mouse_delta;
  float scroll_delta;
} window_events;

struct SerializedReservoir {
  uint32_t y;
  float w;
  float factor;
};

struct FrameDependents {
  ID3D11Texture2D* swapchain_texture;
  ID3D11RenderTargetView* swapchain_rtv;
  
  ID3D11Texture2D* lighting_buffer;
  ID3D11UnorderedAccessView* lighting_buffer_uav;
  ID3D11ShaderResourceView* lighting_buffer_srv;

  ID3D11Buffer* reservoir_buffer;
  ID3D11UnorderedAccessView* reservoir_buffer_uav;

  ID3D11Texture2D* depth_buffer;
  ID3D11DepthStencilView* dsv;

  ID3D11Texture2D* depth_texture;
  ID3D11ShaderResourceView* depth_texture_srv;

  ID3D11Texture2D* gbuffer_albedo;
  ID3D11Texture2D* gbuffer_normal;
  ID3D11RenderTargetView* gbuffer_albedo_rtv;
  ID3D11RenderTargetView* gbuffer_normal_rtv;
  ID3D11ShaderResourceView* gbuffer_albedo_srv;
  ID3D11ShaderResourceView* gbuffer_normal_srv;

  uint32_t w, h;

  uint32_t lighting_w, lighting_h;

  void release() {
    if (swapchain_texture) {
      reservoir_buffer_uav->Release();
      reservoir_buffer->Release();
      gbuffer_normal_srv->Release();
      gbuffer_albedo_srv->Release();
      gbuffer_normal_rtv->Release();
      gbuffer_albedo_rtv->Release();
      gbuffer_normal->Release();
      gbuffer_albedo->Release();
      depth_texture_srv->Release();
      depth_texture->Release();
      dsv->Release();
      depth_buffer->Release();
      lighting_buffer_srv->Release();
      lighting_buffer_uav->Release();
      lighting_buffer->Release();
      swapchain_rtv->Release();
      swapchain_texture->Release();
    }
  }

  void init(ID3D11Device* device, IDXGISwapChain* swapchain) {
    DXGI_SWAP_CHAIN_DESC swapchain_desc;
    swapchain->GetDesc(&swapchain_desc);

    w = swapchain_desc.BufferDesc.Width;
    h = swapchain_desc.BufferDesc.Height;

    swapchain->GetBuffer(0, IID_PPV_ARGS(&swapchain_texture));

    D3D11_RENDER_TARGET_VIEW_DESC swapchain_rtv_rtv_desc = {};
    swapchain_rtv_rtv_desc.Format = swapchain_desc.BufferDesc.Format;
    swapchain_rtv_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    device->CreateRenderTargetView(swapchain_texture, &swapchain_rtv_rtv_desc, &swapchain_rtv);

    D3D11_TEXTURE2D_DESC lighting_buffer_desc = {};
    lighting_buffer_desc.Width = swapchain_desc.BufferDesc.Width >> 1;
    lighting_buffer_desc.Height = swapchain_desc.BufferDesc.Height >> 1;
    lighting_buffer_desc.MipLevels = 1;
    lighting_buffer_desc.ArraySize = 1;
    lighting_buffer_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    lighting_buffer_desc.SampleDesc.Count = 1;
    lighting_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    lighting_buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;

    lighting_w = lighting_buffer_desc.Width;
    lighting_h = lighting_buffer_desc.Height;

    device->CreateTexture2D(&lighting_buffer_desc, nullptr, &lighting_buffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC lighting_buffer_uav_desc = {};
    lighting_buffer_uav_desc.Format = lighting_buffer_desc.Format;
    lighting_buffer_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    device->CreateUnorderedAccessView(lighting_buffer, &lighting_buffer_uav_desc, &lighting_buffer_uav);

    D3D11_SHADER_RESOURCE_VIEW_DESC lighting_buffer_srv_desc = {};
    lighting_buffer_srv_desc.Format = lighting_buffer_desc.Format;
    lighting_buffer_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    lighting_buffer_srv_desc.Texture2D.MipLevels = (UINT)-1;

    device->CreateShaderResourceView(lighting_buffer, &lighting_buffer_srv_desc, &lighting_buffer_srv);

    D3D11_BUFFER_DESC reservoir_buffer_desc = {};
    reservoir_buffer_desc.ByteWidth = lighting_buffer_desc.Width * lighting_buffer_desc.Height * sizeof(SerializedReservoir);
    reservoir_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    reservoir_buffer_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    reservoir_buffer_desc.StructureByteStride = sizeof(SerializedReservoir);
    reservoir_buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;

    device->CreateBuffer(&reservoir_buffer_desc, nullptr, &reservoir_buffer);

    D3D11_UNORDERED_ACCESS_VIEW_DESC reservoir_buffer_uav_desc = {};
    reservoir_buffer_uav_desc.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    reservoir_buffer_uav_desc.Buffer.NumElements = lighting_buffer_desc.Width * lighting_buffer_desc.Height;

    device->CreateUnorderedAccessView(reservoir_buffer, &reservoir_buffer_uav_desc, &reservoir_buffer_uav);

    D3D11_TEXTURE2D_DESC depth_buffer_desc = {};
    depth_buffer_desc.Width = swapchain_desc.BufferDesc.Width;
    depth_buffer_desc.Height = swapchain_desc.BufferDesc.Height;
    depth_buffer_desc.MipLevels = 1;
    depth_buffer_desc.ArraySize = 1;
    depth_buffer_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    depth_buffer_desc.SampleDesc.Count = 1;
    depth_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

    device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_buffer);

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(depth_buffer, &dsv_desc, &dsv);

    D3D11_TEXTURE2D_DESC depth_texture_desc = {};
    depth_texture_desc.Width = swapchain_desc.BufferDesc.Width;
    depth_texture_desc.Height = swapchain_desc.BufferDesc.Height;
    depth_texture_desc.MipLevels = 0;
    depth_texture_desc.ArraySize = 1;
    depth_texture_desc.Format = DXGI_FORMAT_R32_FLOAT;
    depth_texture_desc.SampleDesc.Count = 1;
    depth_texture_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    depth_texture_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    device->CreateTexture2D(&depth_texture_desc, nullptr, &depth_texture);

    D3D11_SHADER_RESOURCE_VIEW_DESC depth_texture_srv_desc = {};
    depth_texture_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    depth_texture_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depth_texture_srv_desc.Texture2D.MipLevels = (UINT)-1;

    device->CreateShaderResourceView(depth_texture, &depth_texture_srv_desc, &depth_texture_srv);

    D3D11_TEXTURE2D_DESC gbuffer_albedo_desc = {};
    gbuffer_albedo_desc.Width = swapchain_desc.BufferDesc.Width;
    gbuffer_albedo_desc.Height = swapchain_desc.BufferDesc.Height;
    gbuffer_albedo_desc.MipLevels = 0;
    gbuffer_albedo_desc.ArraySize = 1;
    gbuffer_albedo_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gbuffer_albedo_desc.SampleDesc.Count = 1;
    gbuffer_albedo_desc.Usage = D3D11_USAGE_DEFAULT;
    gbuffer_albedo_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    gbuffer_albedo_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    device->CreateTexture2D(&gbuffer_albedo_desc, nullptr, &gbuffer_albedo);

    D3D11_TEXTURE2D_DESC gbuffer_normal_desc = {};
    gbuffer_normal_desc.Width = swapchain_desc.BufferDesc.Width;
    gbuffer_normal_desc.Height = swapchain_desc.BufferDesc.Height;
    gbuffer_normal_desc.MipLevels = 0;
    gbuffer_normal_desc.ArraySize = 1;
    gbuffer_normal_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gbuffer_normal_desc.SampleDesc.Count = 1;
    gbuffer_normal_desc.Usage = D3D11_USAGE_DEFAULT;
    gbuffer_normal_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
    gbuffer_normal_desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

    device->CreateTexture2D(&gbuffer_normal_desc, nullptr, &gbuffer_normal);

    D3D11_RENDER_TARGET_VIEW_DESC gbuffer_albedo_rtv_desc = {};
    gbuffer_albedo_rtv_desc.Format = gbuffer_albedo_desc.Format;
    gbuffer_albedo_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    device->CreateRenderTargetView(gbuffer_albedo, &gbuffer_albedo_rtv_desc, &gbuffer_albedo_rtv);

    D3D11_RENDER_TARGET_VIEW_DESC gbuffer_normal_rtv_desc = {};
    gbuffer_normal_rtv_desc.Format = gbuffer_normal_desc.Format;
    gbuffer_normal_rtv_desc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;

    device->CreateRenderTargetView(gbuffer_normal, &gbuffer_normal_rtv_desc, &gbuffer_normal_rtv);

    D3D11_SHADER_RESOURCE_VIEW_DESC gbuffer_albedo_srv_desc = {};
    gbuffer_albedo_srv_desc.Format = gbuffer_albedo_desc.Format;
    gbuffer_albedo_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    gbuffer_albedo_srv_desc.Texture2D.MipLevels = (UINT)-1;

    device->CreateShaderResourceView(gbuffer_albedo, &gbuffer_albedo_srv_desc, &gbuffer_albedo_srv);

    D3D11_SHADER_RESOURCE_VIEW_DESC gbuffer_normal_srv_desc = {};
    gbuffer_normal_srv_desc.Format = gbuffer_normal_desc.Format;
    gbuffer_normal_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    gbuffer_normal_srv_desc.Texture2D.MipLevels = (UINT)-1;

    device->CreateShaderResourceView(gbuffer_normal, &gbuffer_normal_srv_desc, &gbuffer_normal_srv);
  }
};

static LRESULT window_proc(HWND window, UINT msg, WPARAM w_param, LPARAM l_param){
  switch (msg) {
    case WM_CLOSE: {
      window_events.closed = true;
    } break;

    case WM_SIZE: {
      if (w_param != SIZE_MINIMIZED) {
        int32_t width = LOWORD(l_param);
        int32_t height = HIWORD(l_param);

        if (width != 0 && height != 0) {
          window_events.resize = {width, height};
        }
      }
    } break;

    case WM_MBUTTONDOWN: {
      RECT rect;
      GetClientRect(window, &rect);

      int32_t x = LOWORD(l_param);
      int32_t y = HIWORD(l_param);

      if (x > rect.left && x < rect.right && y > rect.top && y < rect.bottom) {
        SetCapture(window);
        ShowCursor(false);

        RECT screen_rect;
        GetWindowRect(window, &screen_rect);

        ClipCursor(&screen_rect);
      }
    } break;

    case WM_MBUTTONUP: {
      if (window == GetCapture()) {
        ShowCursor(true);
        ReleaseCapture();
        ClipCursor(NULL);
      }
    } break;

    case WM_INPUT: {
      UINT size;

      GetRawInputData((HRAWINPUT)l_param, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
      std::vector<char> lpb(size);

      GetRawInputData((HRAWINPUT)l_param, RID_INPUT, lpb.data(), &size, sizeof(RAWINPUTHEADER));

      RAWINPUT* raw = (RAWINPUT*)lpb.data();

      if (raw->header.dwType == RIM_TYPEMOUSE) {
        window_events.mouse_delta.x += raw->data.mouse.lLastX;
        window_events.mouse_delta.y += raw->data.mouse.lLastY;
      }
    } break;

    case WM_MOUSEWHEEL: {
      float delta = (float)GET_WHEEL_DELTA_WPARAM(w_param);
      window_events.scroll_delta += delta;
    } break;
  }

  return DefWindowProcA(window, msg, w_param, l_param);
}

static std::pair<int32_t, int32_t> window_size(HWND window) {
  RECT rect;
  GetClientRect(window, &rect);

  return {
    rect.right-rect.left,
    rect.bottom - rect.top
  };
}

static std::vector<char> load_bin(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return std::vector(std::istreambuf_iterator<char>(input), {});
}

struct CameraCbuffer {
  XMMATRIX inv_view;
  XMMATRIX inv_view_proj;
  XMMATRIX view_proj;
  uint32_t frame;
};

template<typename T>
static std::pair<ID3D11Buffer*, ID3D11ShaderResourceView*> create_immutable_structured_buffer(ID3D11Device* device, T* data, size_t count) {
  assert(count <= UINT_MAX);

  D3D11_BUFFER_DESC buffer_desc = {};
  buffer_desc.ByteWidth = (UINT)(count * sizeof(T));
  buffer_desc.Usage = D3D11_USAGE_IMMUTABLE;
  buffer_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
  buffer_desc.StructureByteStride = sizeof(T);
  buffer_desc.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;


  D3D11_SUBRESOURCE_DATA initial_data = {
    .pSysMem = data,
    .SysMemPitch = buffer_desc.ByteWidth
  };

  ID3D11Buffer* buffer = nullptr;
  device->CreateBuffer(&buffer_desc, &initial_data, &buffer);

  D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
  srv_desc.Format = DXGI_FORMAT_UNKNOWN;
  srv_desc.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
  srv_desc.Buffer.NumElements = (UINT)count;

  ID3D11ShaderResourceView* srv;
  device->CreateShaderResourceView(buffer, &srv_desc, &srv);

  return {buffer, srv};
}

static Mesh combine_model(const Model& model) {
  std::vector<XMFLOAT3> positions;
  std::vector<XMFLOAT3> normals;
  std::vector<XMFLOAT2> tex_coords;
  std::vector<uint32_t> indices;

  uint32_t indices_offset = 0; 

  for (auto& instance : model.instances) {
    auto& mesh = model.meshes[instance.mesh];

    for (auto idx : mesh.indices) {
      indices.push_back(indices_offset + idx);
    }

    for (auto pos : mesh.positions) {
      XMVECTOR p = {pos.x, pos.y, pos.z, 1.0f};
      p = XMVector4Transform(p, instance.transform);
      XMStoreFloat3(&pos, p);
      positions.push_back(pos);
    }

    for (auto norm : mesh.normals) {
      XMVECTOR n = {norm.x, norm.y, norm.z, 0.0f};
      n = XMVector3Normalize(XMVector4Transform(n, instance.transform));
      XMStoreFloat3(&norm, n);
      normals.push_back(norm);
    }

    for (auto tc : mesh.tex_coords) {
      tex_coords.push_back(tc);
    }

    assert(positions.size() == normals.size());
    assert(positions.size() == tex_coords.size());

    indices_offset = (uint32_t)positions.size();
  }

  return Mesh {
    .positions = positions,
    .normals = normals,
    .tex_coords = tex_coords,
    .indices = indices
  };
}

static std::tuple<ID3D11ComputeShader*, uint32_t, uint32_t> create_compute_shader(ID3D11Device* device, const std::vector<char>& code) {
  ID3D11ComputeShader* cs = nullptr;
  device->CreateComputeShader(code.data(), code.size(), nullptr, &cs);
  ID3D11ShaderReflection* cs_refl = nullptr;
  D3DReflect(code.data(), code.size(), IID_PPV_ARGS(&cs_refl));
  UINT w, h;
  cs_refl->GetThreadGroupSize(&w, &h, nullptr);
  cs_refl->Release();

  return {cs, w, h};
}

template<typename T>
struct ConstantBuffer {
  ID3D11Buffer* buffer;

  void init(ID3D11Device* device) {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = (sizeof(T) + 15) & ~15;
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    device->CreateBuffer(&desc, nullptr, &buffer);
  }

  T* map(ID3D11DeviceContext* ctx) {
    D3D11_MAPPED_SUBRESOURCE mapped_camera_cbuffer;
    ctx->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_camera_cbuffer);
    return (T*)mapped_camera_cbuffer.pData;
  }

  void unmap(ID3D11DeviceContext* ctx) {
    ctx->Unmap(buffer, 0);
  }
};

struct ReservoirConstants {
  uint32_t width;
  uint32_t height;
  uint32_t frame;
};

int main() {
  WNDCLASSA wc = {
    .lpfnWndProc = window_proc,
    .hInstance = GetModuleHandleA(nullptr),
    .lpszClassName = "bruh",
  };

  RegisterClassA(&wc);

  RAWINPUTDEVICE rid = {
    .usUsagePage = 0x01,
    .usUsage = 0x02, // mouse
  };

  RegisterRawInputDevices(&rid, 1, sizeof(rid));

  HWND window = CreateWindowExA(0, wc.lpszClassName, "Raywaster", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, wc.hInstance, nullptr);
  ShowWindow(window, SW_SHOWDEFAULT);

  UINT device_flags = 0;

#if _DEBUG
  device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

  auto [initial_w, initial_h] = window_size(window);

  DXGI_SWAP_CHAIN_DESC swapchain_desc = {};
  swapchain_desc.BufferDesc.Width = initial_w;
  swapchain_desc.BufferDesc.Height = initial_h;
  swapchain_desc.BufferDesc.Format = SWAPCHAIN_FORMAT;
  swapchain_desc.SampleDesc.Count = 1;
  swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapchain_desc.BufferCount = 3;
  swapchain_desc.OutputWindow = window;
  swapchain_desc.Windowed = TRUE;
  swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

  IDXGISwapChain* swapchain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* ctx = nullptr;

  if(FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, device_flags, nullptr, 0, D3D11_SDK_VERSION, &swapchain_desc, &swapchain, &device, nullptr, &ctx))) {
    MessageBoxA(nullptr, "Failed to create D3D11 device.", "Error", 0);
    return 1;
  }

  FrameDependents frame_dependents = {};
  frame_dependents.init(device, swapchain);

  std::vector<char> lighting_cs_code = load_bin("bin/lighting_cs.cso");
  std::vector<char> reservoir1_cs_code = load_bin("bin/reservoir1_cs.cso");
  std::vector<char> gbuffer_vs_code = load_bin("bin/gbuffer_vs.cso");
  std::vector<char> gbuffer_ps_code = load_bin("bin/gbuffer_ps.cso");
  std::vector<char> screen_quad_vs_code = load_bin("bin/screen_quad_vs.cso");
  std::vector<char> combine_ps_code = load_bin("bin/combine_ps.cso");

  auto [lighting_cs, lighting_cs_thread_group_x, lighting_cs_thread_group_y] = create_compute_shader(device, lighting_cs_code);
  auto [reservoir1_cs, reservoir1_cs_thread_group_x, reservoir1_cs_thread_group_y] = create_compute_shader(device, reservoir1_cs_code);

  ID3D11VertexShader* gbuffer_vs = nullptr;
  ID3D11PixelShader* gbuffer_ps = nullptr;
  device->CreateVertexShader(gbuffer_vs_code.data(), gbuffer_vs_code.size(), nullptr, &gbuffer_vs);
  device->CreatePixelShader(gbuffer_ps_code.data(), gbuffer_ps_code.size(), nullptr, &gbuffer_ps);

  ID3D11VertexShader* screen_quad_vs = nullptr;
  ID3D11PixelShader* combine_ps = nullptr;
  device->CreateVertexShader(screen_quad_vs_code.data(), screen_quad_vs_code.size(), nullptr, &screen_quad_vs);
  device->CreatePixelShader(combine_ps_code.data(), combine_ps_code.size(), nullptr, &combine_ps);

  D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
  depth_state_desc.DepthEnable = TRUE;
  depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_state_desc.DepthFunc = D3D11_COMPARISON_GREATER;

  ID3D11DepthStencilState* depth_state = nullptr;
  device->CreateDepthStencilState(&depth_state_desc, &depth_state);

  Mesh mesh = combine_model(*load_gltf("models/test/scene.gltf"));

  std::vector<bvh::Node> bvh = bvh::construct_bvh(mesh.positions, mesh.indices);

  auto [positions_buf, positions_srv]   = create_immutable_structured_buffer<XMFLOAT3>(device, mesh.positions.data(),  mesh.positions.size());
  auto [normals_buf, normals_srv]       = create_immutable_structured_buffer<XMFLOAT3>(device, mesh.normals.data(),    mesh.normals.size());
  auto [tex_coords_buf, tex_coords_srv] = create_immutable_structured_buffer<XMFLOAT2>(device, mesh.tex_coords.data(), mesh.tex_coords.size());
  auto [indices_buf, indices_srv]       = create_immutable_structured_buffer<uint32_t>(device, mesh.indices.data(),    mesh.indices.size());
  auto [bvh_buf, bvh_srv]               = create_immutable_structured_buffer<bvh::Node>(device, bvh.data(), bvh.size());

  int hdri_w, hdri_h;
  float* hdri_data = stbi_loadf("sky/symmetrical_garden_02_4k.hdr", &hdri_w, &hdri_h, nullptr, 3);

  if (!hdri_data) {
    std::cout << "Failed to load hdri\n";
    return 1;
  }

  D3D11_TEXTURE2D_DESC hdri_desc = {};
  hdri_desc.Width = hdri_w;
  hdri_desc.Height = hdri_h;
  hdri_desc.MipLevels = 1;
  hdri_desc.ArraySize = 1;
  hdri_desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
  hdri_desc.SampleDesc.Count = 1;
  hdri_desc.Usage = D3D11_USAGE_IMMUTABLE;
  hdri_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

  D3D11_SUBRESOURCE_DATA hdri_subresource_data = {
    .pSysMem = hdri_data,
    .SysMemPitch = hdri_w * 3 * sizeof(float),
    .SysMemSlicePitch = 1
  };

  ID3D11Texture2D* hdri = nullptr;
  device->CreateTexture2D(&hdri_desc, &hdri_subresource_data,&hdri);

  D3D11_SHADER_RESOURCE_VIEW_DESC hdri_srv_desc = {};
  hdri_srv_desc.Format = hdri_desc.Format;
  hdri_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
  hdri_srv_desc.Texture2D.MipLevels = 1;

  ID3D11ShaderResourceView* hdri_srv = nullptr;
  device->CreateShaderResourceView(hdri, &hdri_srv_desc, &hdri_srv);

  D3D11_SAMPLER_DESC linear_wrap_sampler_desc = {
    .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
    .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
    .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
    .MaxLOD = D3D11_FLOAT32_MAX
  };

  D3D11_SAMPLER_DESC linear_clamp_sampler_desc = {
    .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
    .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
    .MaxLOD = D3D11_FLOAT32_MAX
  };

  D3D11_SAMPLER_DESC point_clamp_sampler_desc = {
    .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
    .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
    .MaxLOD = D3D11_FLOAT32_MAX
  };

  ID3D11SamplerState* linear_wrap_sampler = nullptr;
  ID3D11SamplerState* point_clamp_sampler = nullptr;
  ID3D11SamplerState* linear_clamp_sampler = nullptr;
  device->CreateSamplerState(&linear_wrap_sampler_desc, &linear_wrap_sampler);
  device->CreateSamplerState(&point_clamp_sampler_desc, &point_clamp_sampler);
  device->CreateSamplerState(&linear_clamp_sampler_desc, &linear_clamp_sampler);

  ConstantBuffer<CameraCbuffer> camera_cbuffer;
  ConstantBuffer<ReservoirConstants> reservoir_cbuffer;

  camera_cbuffer.init(device);
  reservoir_cbuffer.init(device);

  auto timer_start = std::chrono::steady_clock::now();
  int timer_count = 0;

  XMVECTOR camera_focus = {0.0f, 0.0f, 0.0f};
  float camera_theta = XM_PI * 0.5f;
  float camera_phi = XM_PI * 0.25f;
  float camera_distance = 6.0f;
  float camera_sensitivity = 2e-3f;
  float camera_speed = 2e-3f;

  uint32_t frame = 0;

  for (;;) {
    frame++;

    window_events = {};

    MSG msg;
    while (PeekMessageA(&msg, window, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageA(&msg);
    }

    if (window_events.closed) {
      break;
    }

    if (auto size = window_events.resize) {
      auto [w, h] = *size;
      frame_dependents.release();
      swapchain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
      frame_dependents.init(device, swapchain);
    }

    XMVECTOR camera_offset = {
      camera_distance * std::sin(camera_phi) * std::cos(camera_theta),
      camera_distance * std::cos(camera_phi),
      camera_distance * std::sin(camera_phi) * std::sin(camera_theta),
    };

    XMVECTOR camera_right = XMVector3Normalize(XMVector3Cross({0.0f, 1.0f, 0.0f}, XMVector3Normalize(camera_offset)));
    XMVECTOR camera_up = XMVector3Normalize(XMVector3Cross(XMVector3Normalize(camera_offset), camera_right));

    if (GetCapture() == window) {
      if (GetKeyState(VK_SHIFT) & 0x8000) {
        camera_focus -= camera_right * camera_speed * window_events.mouse_delta.x;
        camera_focus += camera_up * camera_speed * window_events.mouse_delta.y;
      }
      else {
        camera_theta += camera_sensitivity * window_events.mouse_delta.x;
        camera_phi = std::clamp(camera_phi - camera_sensitivity * window_events.mouse_delta.y, XM_PI*0.05f, XM_PI*0.95f);
      }
    }

    POINT cursor_pos;
    GetCursorPos(&cursor_pos);

    RECT window_rect;
    GetWindowRect(window, &window_rect);

    if (GetFocus() == window && cursor_pos.x > window_rect.left && cursor_pos.x < window_rect.right && cursor_pos.y > window_rect.top && cursor_pos.y < window_rect.bottom) {
      camera_distance -= camera_distance * 1e-3f * window_events.scroll_delta;
    }

    XMMATRIX view = XMMatrixLookAtRH(camera_offset + camera_focus, camera_focus, {0.0f, 1.0f, 0.0f});
    XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PI*0.25f, (float)frame_dependents.w/(float)frame_dependents.h, 1000.0f, 0.01f);
    XMMATRIX view_proj = view * proj;

    CameraCbuffer* camera_cbuffer_data = camera_cbuffer.map(ctx);
    camera_cbuffer_data->inv_view = XMMatrixInverse(nullptr, view);
    camera_cbuffer_data->inv_view_proj = XMMatrixInverse(nullptr, view_proj);
    camera_cbuffer_data->view_proj = view_proj;
    camera_cbuffer_data->frame = frame;
    camera_cbuffer.unmap(ctx);

    float clear_color[4] = {};
    ctx->ClearRenderTargetView(frame_dependents.gbuffer_albedo_rtv, clear_color);
    ctx->ClearRenderTargetView(frame_dependents.gbuffer_normal_rtv, clear_color);
    ctx->ClearDepthStencilView(frame_dependents.dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);

    ctx->PSSetShader(gbuffer_ps, nullptr, 0);
    ctx->VSSetShader(gbuffer_vs, nullptr, 0);

    ctx->VSSetConstantBuffers(0, 1, &camera_cbuffer.buffer);

    ID3D11ShaderResourceView* gbuffer_srvs_bind[] = {
      positions_srv,
      normals_srv,
      tex_coords_srv,
      indices_srv,
    };

    ctx->VSSetShaderResources(0, std::size(gbuffer_srvs_bind), gbuffer_srvs_bind);

    D3D11_VIEWPORT viewport = {
      .Width  = (float)frame_dependents.w,
      .Height = (float)frame_dependents.h,
      .MaxDepth = 1.0f
    };

    D3D11_RECT scissor = {
      .right  = (LONG)frame_dependents.w,
      .bottom = (LONG)frame_dependents.h,
    };

    ctx->RSSetViewports(1, &viewport);
    ctx->RSSetScissorRects(1, &scissor);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    ID3D11RenderTargetView* render_targets_bind[] = {
      frame_dependents.gbuffer_albedo_rtv,
      frame_dependents.gbuffer_normal_rtv,
    };

    ctx->OMSetRenderTargets(std::size(render_targets_bind), render_targets_bind, frame_dependents.dsv);
    ctx->OMSetDepthStencilState(depth_state, 0);

    ctx->Draw((UINT)mesh.indices.size(), 0);
    ctx->OMSetRenderTargets(0, nullptr, nullptr);

    ctx->CopySubresourceRegion(frame_dependents.depth_texture, 0, 0, 0, 0, frame_dependents.depth_buffer, 0, nullptr);

    ctx->GenerateMips(frame_dependents.depth_texture_srv);
    ctx->GenerateMips(frame_dependents.gbuffer_albedo_srv);
    ctx->GenerateMips(frame_dependents.gbuffer_normal_srv);

    // generate reservoirs

    ReservoirConstants* reservoir_constants = reservoir_cbuffer.map(ctx);
    reservoir_constants->width = frame_dependents.lighting_w;
    reservoir_constants->height = frame_dependents.lighting_h;
    reservoir_constants->frame = frame;
    reservoir_cbuffer.unmap(ctx);

    ctx->CSSetShader(reservoir1_cs, nullptr, 0);
    ctx->CSSetConstantBuffers(0, 1, &reservoir_cbuffer.buffer);
    ctx->CSSetUnorderedAccessViews(0, 1, &frame_dependents.reservoir_buffer_uav, nullptr);

    ID3D11ShaderResourceView* reservoir1_srv_binds[] = {
      frame_dependents.gbuffer_normal_srv,
      hdri_srv,
    };

    ctx->CSSetShaderResources(0, std::size(reservoir1_srv_binds), reservoir1_srv_binds);

    ID3D11SamplerState* reservoir1_sampler_binds[] = {
      point_clamp_sampler,
      linear_wrap_sampler
    };

    ctx->CSSetSamplers(0, std::size(reservoir1_sampler_binds), reservoir1_sampler_binds);
    ctx->Dispatch((frame_dependents.lighting_w+reservoir1_cs_thread_group_x-1)/reservoir1_cs_thread_group_x, (frame_dependents.lighting_h+reservoir1_cs_thread_group_y-1)/reservoir1_cs_thread_group_y, 1);

    // lighting pass

    ctx->CSSetShader(lighting_cs, nullptr, 0);

    ctx->CSSetConstantBuffers(0, 1, &camera_cbuffer.buffer);

    ID3D11ShaderResourceView* cs_srvs_bind[] = {
      positions_srv,
      normals_srv,
      tex_coords_srv,
      indices_srv,
      bvh_srv,
      hdri_srv,
      frame_dependents.depth_texture_srv,
      frame_dependents.gbuffer_albedo_srv,
      frame_dependents.gbuffer_normal_srv,
    };

    ctx->CSSetShaderResources(0, std::size(cs_srvs_bind), cs_srvs_bind);

    ID3D11SamplerState* lighting_samplers_bind[] = {
      linear_wrap_sampler,
      point_clamp_sampler
    };

    ctx->CSSetSamplers(0, std::size(lighting_samplers_bind), lighting_samplers_bind);

    ID3D11UnorderedAccessView* lighting_uavs_bind[] = {
      frame_dependents.lighting_buffer_uav,
      frame_dependents.reservoir_buffer_uav
    };

    ctx->CSSetUnorderedAccessViews(0, std::size(lighting_uavs_bind), lighting_uavs_bind, nullptr);
    ctx->Dispatch((frame_dependents.lighting_w+lighting_cs_thread_group_x-1)/lighting_cs_thread_group_x, (frame_dependents.lighting_h+lighting_cs_thread_group_y-1)/lighting_cs_thread_group_y, 1);

    memset(lighting_uavs_bind, 0, sizeof(lighting_uavs_bind));
    ctx->CSSetUnorderedAccessViews(0, std::size(lighting_uavs_bind), lighting_uavs_bind, nullptr);

    memset(cs_srvs_bind, 0, sizeof(cs_srvs_bind));
    ctx->CSSetShaderResources(0, std::size(cs_srvs_bind), cs_srvs_bind);

    // combine pass

    ctx->VSSetShader(screen_quad_vs, nullptr, 0);
    ctx->PSSetShader(combine_ps, nullptr, 0);

    ID3D11ShaderResourceView* combine_srvs_bind[] = {
      frame_dependents.lighting_buffer_srv,
      hdri_srv,
      frame_dependents.depth_texture_srv,
    };

    ctx->PSSetShaderResources(0, std::size(combine_srvs_bind), combine_srvs_bind);

    ctx->PSSetConstantBuffers(0, 1, &camera_cbuffer.buffer);

    ID3D11SamplerState* combine_samplers_bind[] = {
      point_clamp_sampler,
      linear_clamp_sampler,
      linear_wrap_sampler
    };

    ctx->PSSetSamplers(0, std::size(combine_samplers_bind), combine_samplers_bind);

    ctx->OMSetRenderTargets(1, &frame_dependents.swapchain_rtv, nullptr); 
    ctx->Draw(6, 0);

    memset(combine_srvs_bind, 0, sizeof(combine_srvs_bind));
    ctx->PSSetShaderResources(0, std::size(combine_srvs_bind), combine_srvs_bind);

    swapchain->Present(0, 0); 

    if (++timer_count == 256) {
      auto timer_end = std::chrono::steady_clock::now();
      auto diff = (float)std::chrono::duration_cast<std::chrono::microseconds>(timer_end-timer_start).count()/float(timer_count) * 1e-3;
      std::cout << std::format("frame-time: {} ms\n",  diff);

      timer_count = 0;
      timer_start = timer_end;
    }
  }

  return 0;
}