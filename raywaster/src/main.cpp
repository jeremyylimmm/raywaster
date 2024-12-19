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

struct FrameDependents {
  ID3D11Texture2D* swapchain_texture;
  
  ID3D11Texture2D* rt0;
  ID3D11UnorderedAccessView* rt0_uav;

  ID3D11Texture2D* depth_buffer;
  ID3D11DepthStencilView* dsv;
  ID3D11ShaderResourceView* depth_buffer_srv;

  ID3D11Texture2D* gbuffer_albedo;
  ID3D11Texture2D* gbuffer_normal;
  ID3D11RenderTargetView* gbuffer_albedo_rtv;
  ID3D11RenderTargetView* gbuffer_normal_rtv;
  ID3D11ShaderResourceView* gbuffer_albedo_srv;
  ID3D11ShaderResourceView* gbuffer_normal_srv;

  uint32_t w, h;

  void release() {
    if (swapchain_texture) {
      gbuffer_normal_srv->Release();
      gbuffer_albedo_srv->Release();
      gbuffer_normal_rtv->Release();
      gbuffer_albedo_rtv->Release();
      gbuffer_normal->Release();
      gbuffer_albedo->Release();
      depth_buffer_srv->Release();
      dsv->Release();
      depth_buffer->Release();
      rt0_uav->Release();
      rt0->Release();
      swapchain_texture->Release();
    }
  }

  void init(ID3D11Device* device, IDXGISwapChain* swapchain) {
    DXGI_SWAP_CHAIN_DESC swapchain_desc;
    swapchain->GetDesc(&swapchain_desc);

    w = swapchain_desc.BufferDesc.Width;
    h = swapchain_desc.BufferDesc.Height;

    swapchain->GetBuffer(0, IID_PPV_ARGS(&swapchain_texture));

    D3D11_TEXTURE2D_DESC rt0_desc = {};
    rt0_desc.Width = swapchain_desc.BufferDesc.Width;
    rt0_desc.Height = swapchain_desc.BufferDesc.Height;
    rt0_desc.MipLevels = 1;
    rt0_desc.ArraySize = 1;
    rt0_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rt0_desc.SampleDesc.Count = 1;
    rt0_desc.Usage = D3D11_USAGE_DEFAULT;
    rt0_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_RENDER_TARGET;

    device->CreateTexture2D(&rt0_desc, nullptr, &rt0);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = rt0_desc.Format;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    device->CreateUnorderedAccessView(rt0, &uav_desc, &rt0_uav);

    D3D11_TEXTURE2D_DESC depth_buffer_desc = {};
    depth_buffer_desc.Width = swapchain_desc.BufferDesc.Width;
    depth_buffer_desc.Height = swapchain_desc.BufferDesc.Height;
    depth_buffer_desc.MipLevels = 1;
    depth_buffer_desc.ArraySize = 1;
    depth_buffer_desc.Format = DXGI_FORMAT_R32_TYPELESS;
    depth_buffer_desc.SampleDesc.Count = 1;
    depth_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;

    device->CreateTexture2D(&depth_buffer_desc, nullptr, &depth_buffer);

    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
    dsv_desc.Format = DXGI_FORMAT_D32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(depth_buffer, &dsv_desc, &dsv);

    D3D11_SHADER_RESOURCE_VIEW_DESC depth_buffer_srv_desc = {};
    depth_buffer_srv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    depth_buffer_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    depth_buffer_srv_desc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(depth_buffer, &depth_buffer_srv_desc, &depth_buffer_srv);

    D3D11_TEXTURE2D_DESC gbuffer_albedo_desc = {};
    gbuffer_albedo_desc.Width = swapchain_desc.BufferDesc.Width;
    gbuffer_albedo_desc.Height = swapchain_desc.BufferDesc.Height;
    gbuffer_albedo_desc.MipLevels = 1;
    gbuffer_albedo_desc.ArraySize = 1;
    gbuffer_albedo_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gbuffer_albedo_desc.SampleDesc.Count = 1;
    gbuffer_albedo_desc.Usage = D3D11_USAGE_DEFAULT;
    gbuffer_albedo_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    device->CreateTexture2D(&gbuffer_albedo_desc, nullptr, &gbuffer_albedo);

    D3D11_TEXTURE2D_DESC gbuffer_normal_desc = {};
    gbuffer_normal_desc.Width = swapchain_desc.BufferDesc.Width;
    gbuffer_normal_desc.Height = swapchain_desc.BufferDesc.Height;
    gbuffer_normal_desc.MipLevels = 1;
    gbuffer_normal_desc.ArraySize = 1;
    gbuffer_normal_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    gbuffer_normal_desc.SampleDesc.Count = 1;
    gbuffer_normal_desc.Usage = D3D11_USAGE_DEFAULT;
    gbuffer_normal_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

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
    gbuffer_albedo_srv_desc.Texture2D.MipLevels = 1;

    device->CreateShaderResourceView(gbuffer_albedo, &gbuffer_albedo_srv_desc, &gbuffer_albedo_srv);

    D3D11_SHADER_RESOURCE_VIEW_DESC gbuffer_normal_srv_desc = {};
    gbuffer_normal_srv_desc.Format = gbuffer_normal_desc.Format;
    gbuffer_normal_srv_desc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    gbuffer_normal_srv_desc.Texture2D.MipLevels = 1;

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
  std::vector<char> gbuffer_vs_code = load_bin("bin/gbuffer_vs.cso");
  std::vector<char> gbuffer_ps_code = load_bin("bin/gbuffer_ps.cso");

  ID3D11ComputeShader* lighting_cs = nullptr;
  device->CreateComputeShader(lighting_cs_code.data(), lighting_cs_code.size(), nullptr, &lighting_cs);
  ID3D11ShaderReflection* cs_refl = nullptr;
  D3DReflect(lighting_cs_code.data(), lighting_cs_code.size(), IID_PPV_ARGS(&cs_refl));
  UINT lighting_cs_thread_group_x, lighting_cs_thread_group_y;
  cs_refl->GetThreadGroupSize(&lighting_cs_thread_group_x, &lighting_cs_thread_group_y, nullptr);
  cs_refl->Release();

  ID3D11VertexShader* gbuffer_vs = nullptr;
  ID3D11PixelShader* gbuffer_ps = nullptr;
  device->CreateVertexShader(gbuffer_vs_code.data(), gbuffer_vs_code.size(), nullptr, &gbuffer_vs);
  device->CreatePixelShader(gbuffer_ps_code.data(), gbuffer_ps_code.size(), nullptr, &gbuffer_ps);

  D3D11_DEPTH_STENCIL_DESC depth_state_desc = {};
  depth_state_desc.DepthEnable = TRUE;
  depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
  depth_state_desc.DepthFunc = D3D11_COMPARISON_GREATER;

  ID3D11DepthStencilState* depth_state = nullptr;
  device->CreateDepthStencilState(&depth_state_desc, &depth_state);

  D3D11_BUFFER_DESC camera_cbuffer_desc = {};
  camera_cbuffer_desc.ByteWidth = sizeof(CameraCbuffer);
  camera_cbuffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  camera_cbuffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  camera_cbuffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ID3D11Buffer* camera_cbuffer = nullptr;
  device->CreateBuffer(&camera_cbuffer_desc, nullptr, &camera_cbuffer);

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
  };

  D3D11_SAMPLER_DESC point_clamp_sampler_desc = {
    .Filter = D3D11_FILTER_MIN_MAG_MIP_POINT,
    .AddressU = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressV = D3D11_TEXTURE_ADDRESS_CLAMP,
    .AddressW = D3D11_TEXTURE_ADDRESS_CLAMP,
  };

  ID3D11SamplerState* linear_wrap_sampler = nullptr;
  ID3D11SamplerState* point_clamp_sampler = nullptr;
  device->CreateSamplerState(&linear_wrap_sampler_desc, &linear_wrap_sampler);
  device->CreateSamplerState(&point_clamp_sampler_desc, &point_clamp_sampler);

  auto timer_start = std::chrono::steady_clock::now();
  int timer_count = 0;

  XMVECTOR camera_focus = {0.0f, 0.0f, 0.0f};
  float camera_theta = XM_PI * 0.5f;
  float camera_phi = XM_PI * 0.25f;
  float camera_distance = 6.0f;
  float camera_sensitivity = 2e-3f;
  float camera_speed = 2e-3f;

  for (;;) {
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

    D3D11_MAPPED_SUBRESOURCE mapped_camera_cbuffer;
    ctx->Map(camera_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_camera_cbuffer);

    CameraCbuffer* camera_cbuffer_data = (CameraCbuffer*)mapped_camera_cbuffer.pData;
    camera_cbuffer_data->inv_view = XMMatrixInverse(nullptr, view);
    camera_cbuffer_data->inv_view_proj = XMMatrixInverse(nullptr, view_proj);
    camera_cbuffer_data->view_proj = view_proj;

    ctx->Unmap(camera_cbuffer, 0);

    float clear_color[4] = {};
    ctx->ClearRenderTargetView(frame_dependents.gbuffer_albedo_rtv, clear_color);
    ctx->ClearRenderTargetView(frame_dependents.gbuffer_normal_rtv, clear_color);
    ctx->ClearDepthStencilView(frame_dependents.dsv, D3D11_CLEAR_DEPTH, 0.0f, 0);

    ctx->PSSetShader(gbuffer_ps, nullptr, 0);
    ctx->VSSetShader(gbuffer_vs, nullptr, 0);

    ctx->VSSetConstantBuffers(0, 1, &camera_cbuffer);

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

    ctx->CSSetShader(lighting_cs, nullptr, 0);

    ctx->CSSetUnorderedAccessViews(0, 1, &frame_dependents.rt0_uav, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &camera_cbuffer);

    ID3D11ShaderResourceView* cs_srvs_bind[] = {
      positions_srv,
      normals_srv,
      tex_coords_srv,
      indices_srv,
      bvh_srv,
      hdri_srv,
      frame_dependents.depth_buffer_srv,
      frame_dependents.gbuffer_albedo_srv,
      frame_dependents.gbuffer_normal_srv,
    };

    ctx->CSSetShaderResources(0, std::size(cs_srvs_bind), cs_srvs_bind);

    ID3D11SamplerState* samplers_bind[] = {
      linear_wrap_sampler,
      point_clamp_sampler
    };

    ctx->CSSetSamplers(0, std::size(samplers_bind), samplers_bind);

    ctx->Dispatch((frame_dependents.w+lighting_cs_thread_group_x-1)/lighting_cs_thread_group_x, (frame_dependents.h+lighting_cs_thread_group_y-1)/lighting_cs_thread_group_y, 1);

    // clear that shit
    memset(cs_srvs_bind, 0, sizeof(cs_srvs_bind));
    ctx->CSSetShaderResources(0, std::size(cs_srvs_bind), cs_srvs_bind);

    ctx->CopyResource(frame_dependents.swapchain_texture, frame_dependents.rt0);

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