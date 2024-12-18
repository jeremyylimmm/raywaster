#include <Windows.h>
#include <dxgi.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

#include <iostream>
#include <utility>
#include <optional>
#include <format>
#include <vector>
#include <fstream>
#include <chrono>

#include "model.h"
#include "bvh.h"

using namespace DirectX;

static constexpr DXGI_FORMAT SWAPCHAIN_FORMAT = DXGI_FORMAT_R8G8B8A8_UNORM;

static struct {
  bool closed;
  std::optional<std::pair<int32_t, int32_t>> resize;
  bool resized;
} window_events;

struct FrameDependents {
  ID3D11Texture2D* swapchain_texture;
  
  ID3D11Texture2D* rt0;
  ID3D11UnorderedAccessView* rt0_uav;

  uint32_t w, h;

  void release() {
    if (swapchain_texture) {
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
    rt0_desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;

    device->CreateTexture2D(&rt0_desc, nullptr, &rt0);

    D3D11_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
    uav_desc.Format = rt0_desc.Format;
    uav_desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;

    device->CreateUnorderedAccessView(rt0, &uav_desc, &rt0_uav);
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

int main() {
  WNDCLASSA wc = {
    .lpfnWndProc = window_proc,
    .hInstance = GetModuleHandleA(nullptr),
    .lpszClassName = "bruh",
  };

  RegisterClassA(&wc);

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

  std::vector<char> cs_code = load_bin("bin/test_compute.cso");

  ID3D11ComputeShader* compute_shader = nullptr;
  device->CreateComputeShader(cs_code.data(), cs_code.size(), nullptr, &compute_shader);

  ID3D11ShaderReflection* cs_refl = nullptr;
  D3DReflect(cs_code.data(), cs_code.size(), IID_PPV_ARGS(&cs_refl));

  UINT cs_thread_group_x, cs_thread_group_y;
  cs_refl->GetThreadGroupSize(&cs_thread_group_x, &cs_thread_group_y, nullptr);

  cs_refl->Release();

  D3D11_BUFFER_DESC camera_cbuffer_desc = {};
  camera_cbuffer_desc.ByteWidth = sizeof(CameraCbuffer);
  camera_cbuffer_desc.Usage = D3D11_USAGE_DYNAMIC;
  camera_cbuffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
  camera_cbuffer_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

  ID3D11Buffer* camera_cbuffer = nullptr;
  device->CreateBuffer(&camera_cbuffer_desc, nullptr, &camera_cbuffer);

  Model model = *load_gltf("models/damaged_helmet_embedded/scene.gltf");

  Mesh& mesh0 = model.meshes[0];

  std::vector<bvh::Node> bvh = bvh::construct_bvh(mesh0.positions, mesh0.indices);
  (void)bvh;

  auto [positions_buf, positions_srv]   = create_immutable_structured_buffer<XMFLOAT3>(device, mesh0.positions.data(), mesh0.positions.size());
  auto [normals_buf, normals_srv]       = create_immutable_structured_buffer<XMFLOAT3>(device, mesh0.normals.data(), mesh0.normals.size());
  auto [tex_coords_buf, tex_coords_srv] = create_immutable_structured_buffer<XMFLOAT2>(device, mesh0.tex_coords.data(), mesh0.tex_coords.size());
  auto [indices_buf, indices_srv]       = create_immutable_structured_buffer<uint32_t>(device, mesh0.indices.data(), mesh0.indices.size());
  auto [bvh_buf, bvh_srv]               = create_immutable_structured_buffer<bvh::Node>(device, bvh.data(), bvh.size());

  auto start = std::chrono::steady_clock::now();

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

    ctx->CSSetShader(compute_shader, nullptr, 0);

    auto now = std::chrono::steady_clock::now();
    float time = (float)std::chrono::duration_cast<std::chrono::microseconds>(now - start).count() * 1e-6f;

    XMMATRIX view = XMMatrixLookAtRH({cosf(time) * 6.0f, 2.0f, sinf(time) * 6.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 1.0f, 0.0f});
    XMMATRIX proj = XMMatrixPerspectiveFovRH(XM_PI*0.25f, (float)frame_dependents.w/(float)frame_dependents.h, 0.01f, 1000.0f);

    D3D11_MAPPED_SUBRESOURCE mapped_camera_cbuffer;
    ctx->Map(camera_cbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_camera_cbuffer);

    CameraCbuffer* camera_cbuffer_data = (CameraCbuffer*)mapped_camera_cbuffer.pData;
    camera_cbuffer_data->inv_view = XMMatrixInverse(nullptr, view);
    camera_cbuffer_data->inv_view_proj = XMMatrixInverse(nullptr, view * proj);

    ctx->Unmap(camera_cbuffer, 0);

    ctx->CSSetUnorderedAccessViews(0, 1, &frame_dependents.rt0_uav, nullptr);
    ctx->CSSetConstantBuffers(0, 1, &camera_cbuffer);

    ID3D11ShaderResourceView* srvs_bind[] = {
      positions_srv,
      normals_srv,
      tex_coords_srv,
      indices_srv,
      bvh_srv
    };

    ctx->CSSetShaderResources(0, std::size(srvs_bind), srvs_bind);

    ctx->Dispatch((frame_dependents.w+cs_thread_group_x-1)/cs_thread_group_x, (frame_dependents.h+cs_thread_group_y-1)/cs_thread_group_y, 1);

    ctx->CopyResource(frame_dependents.swapchain_texture, frame_dependents.rt0);

    swapchain->Present(0, 0);
  }

  return 0;
}