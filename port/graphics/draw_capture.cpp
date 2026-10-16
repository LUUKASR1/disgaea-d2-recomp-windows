// clang-format off: bcrypt requires Windows typedefs first.
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#include "draw_capture.h"
#include "../vulkan/draw_packet.h"
#include "capture.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <d3d11shader.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
template <class T> struct Ref {
  T *p = nullptr;
  ~Ref() {
    if (p)
      p->Release();
  }
  T **put() { return &p; }
  T *operator->() { return p; }
};
void check(HRESULT hr) {
  if (FAILED(hr))
    throw std::runtime_error("D3D12 draw capture GPU failure");
}
std::string hash(const void *p, size_t n) {
  if (!p || !n || n > 2097152)
    return {};
  BCRYPT_ALG_HANDLE a = nullptr;
  unsigned char digest[32]{};
  auto hr =
      BCryptOpenAlgorithmProvider(&a, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
  if (hr >= 0)
    hr = BCryptHash(a, nullptr, 0, (PUCHAR)p, ULONG(n), digest, sizeof(digest));
  if (a)
    BCryptCloseAlgorithmProvider(a, 0);
  if (hr < 0)
    return {};
  return std::string((char *)digest, sizeof(digest));
}
struct Pipeline {
  Ref<ID3D12PipelineState> pso;
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};
  std::string vertex, fragment;
  bool color_outputs_supported = false;
  uint32_t output_mask = 0;
};
struct Srv {
  ID3D12Resource *resource = nullptr;
  D3D12_SHADER_RESOURCE_VIEW_DESC view{};
  std::shared_ptr<ID3D12Resource> keep;
};
std::map<std::string, std::string> sources;
std::map<void *, std::unique_ptr<Pipeline>> pipelines;
std::map<uint32_t, Srv> srvs;
size_t source_bytes = 0, pipeline_bytes = 0, exported_bytes = 0;
struct Context {
  Ref<ID3D12CommandAllocator> allocator;
  Ref<ID3D12GraphicsCommandList> list;
  Ref<ID3D12Resource> color, depth;
  Ref<ID3D12DescriptorHeap> rtv, dsv;
  Ref<ID3D12Fence> fence;
  std::vector<ID3D12Resource *> retained;
  std::vector<std::unique_ptr<Ref<ID3D12Resource>>> buffers;
  std::vector<IUnknown *> borrowed;
  HANDLE event = nullptr;
  ~Context() {
    for (auto p : retained)
      p->Release();
    for (auto p : borrowed)
      p->Release();
    if (event)
      CloseHandle(event);
  }
};
struct Copy {
  ID3D12Resource *resource;
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  uint64_t bytes;
};
Copy copy(Context &c, ID3D12Device *device, ID3D12Resource *source,
          D3D12_RESOURCE_STATES prior) {
  auto desc = source->GetDesc();
  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
  UINT rows;
  UINT64 rowbytes, bytes;
  device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowbytes,
                                &bytes);
  if (!bytes || bytes > 67108864)
    throw std::runtime_error("Readback size limit");
  auto b = std::make_unique<Ref<ID3D12Resource>>();
  D3D12_HEAP_PROPERTIES heap{};
  heap.Type = D3D12_HEAP_TYPE_READBACK;
  D3D12_RESOURCE_DESC rd{};
  rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  rd.Width = bytes;
  rd.Height = 1;
  rd.DepthOrArraySize = rd.MipLevels = 1;
  rd.SampleDesc.Count = 1;
  rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &rd,
                                        D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                        IID_ID3D12Resource, (void **)b->put()));
  source->AddRef();
  c.retained.push_back(source);
  D3D12_RESOURCE_BARRIER barrier{};
  barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
  barrier.Transition = {source, 0, prior, D3D12_RESOURCE_STATE_COPY_SOURCE};
  c.list->ResourceBarrier(1, &barrier);
  D3D12_TEXTURE_COPY_LOCATION from{}, to{};
  from.pResource = source;
  from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  to.pResource = b->p;
  to.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  to.PlacedFootprint = footprint;
  c.list->CopyTextureRegion(&to, 0, 0, 0, &from, nullptr);
  barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
  barrier.Transition.StateAfter = prior;
  c.list->ResourceBarrier(1, &barrier);
  Copy out{b->p, footprint, bytes};
  c.buffers.push_back(std::move(b));
  return out;
}
std::vector<uint8_t> rgba(const Copy &c, uint32_t mapping) {
  auto &f = c.footprint.Footprint;
  unsigned bpp = f.Format == DXGI_FORMAT_R8_UNORM     ? 1
                 : f.Format == DXGI_FORMAT_R8G8_UNORM ? 2
                                                      : 4;
  std::vector<uint8_t> output(size_t(f.Width) * f.Height * 4);
  void *data = nullptr;
  D3D12_RANGE range{0, size_t(c.bytes)};
  check(c.resource->Map(0, &range, &data));
  for (unsigned y = 0; y < f.Height; y++)
    for (unsigned x = 0; x < f.Width; x++) {
      auto p = (uint8_t *)data + c.footprint.Offset + size_t(y) * f.RowPitch +
               x * bpp;
      uint8_t channels[6] = {p[0],
                             uint8_t(bpp >= 2 ? p[1] : 0),
                             uint8_t(bpp == 4 ? p[2] : 0),
                             uint8_t(bpp == 4 ? p[3] : 255),
                             0,
                             255};
      for (unsigned k = 0; k < 4; k++)
        output[(size_t(y) * f.Width + x) * 4 + k] =
            channels[(mapping >> (3 * k)) & 7];
    }
  D3D12_RANGE none{0, 0};
  c.resource->Unmap(0, &none);
  return output;
}
std::vector<uint8_t> blocks(const Copy &c, unsigned format) {
  const auto &f = c.footprint.Footprint;
  size_t row = size_t((f.Width + 3) / 4) * (format == 1 ? 8 : 16);
  unsigned rows = (f.Height + 3) / 4;
  std::vector<uint8_t> output(row * rows);
  void *data = nullptr;
  D3D12_RANGE range{0, size_t(c.bytes)};
  check(c.resource->Map(0, &range, &data));
  for (unsigned y = 0; y < rows; ++y)
    std::memcpy(output.data() + y * row,
                (uint8_t *)data + c.footprint.Offset + size_t(y) * f.RowPitch,
                row);
  D3D12_RANGE none{0, 0};
  c.resource->Unmap(0, &none);
  return output;
}
void write(const std::string &name, const void *data, size_t bytes) {
  FILE *f = d2_capture_fopen(name.c_str(), "wb");
  if (!f)
    throw std::runtime_error("Cannot open draw file");
  auto n = fwrite(data, 1, bytes, f);
  auto closed = fclose(f);
  if (n != bytes || closed)
    throw std::runtime_error("Cannot write draw file");
}
void status(const char *folder, const std::string &text) {
  write(std::string(folder) + "/draw.json", text.data(), text.size());
}
} // namespace
extern "C" int d2_draw_capture_enabled() {
  static int enabled = -1;
  if (enabled < 0) {
    char e[4]{};
    enabled =
        GetEnvironmentVariableA("D2_DRAW_CAPTURE", e, 4) == 1 && e[0] == '1';
  }
  return enabled;
}
extern "C" void d2_draw_capture_close() {
  pipelines.clear();
  sources.clear();
  srvs.clear();
  source_bytes = pipeline_bytes = exported_bytes = 0;
}
extern "C" void d2_draw_frame_begin() {
  if (d2_draw_capture_enabled())
    srvs.clear();
}
extern "C" void d2_draw_vertex_source(const void *code, size_t bytes,
                                      const char *hlsl) {
  if (!d2_draw_capture_enabled() || !hlsl)
    return;
  try {
    size_t n = strnlen(hlsl, 262145);
    if (!n || n > 262144 || sources.size() >= 128 ||
        n > 16777216 - source_bytes)
      return;
    auto key = hash(code, bytes);
    if (key.empty() || sources.count(key))
      return;
    sources.emplace(key, hlsl);
    source_bytes += n;
  } catch (...) {
  }
}
extern "C" void d2_draw_pipeline(void *pso, const void *description,
                                 const char *hlsl) {
  if (!d2_draw_capture_enabled() || !pso || !description || !hlsl)
    return;
  try {
    if (pipelines.count(pso) || pipelines.size() >= 128)
      return;
    auto &pd = *(const D3D12_GRAPHICS_PIPELINE_STATE_DESC *)description;
    auto it = sources.find(hash(pd.VS.pShaderBytecode, pd.VS.BytecodeLength));
    size_t n = strnlen(hlsl, 262145);
    if (it == sources.end() || !n || n > 262144 ||
        n + it->second.size() > 33554432 - pipeline_bytes)
      return;
    auto p = std::make_unique<Pipeline>();
    p->pd = pd;
    p->vertex = it->second;
    p->fragment = hlsl;
    Ref<ID3D11ShaderReflection> reflection;
    if (SUCCEEDED(D3DReflect(pd.PS.pShaderBytecode, pd.PS.BytecodeLength,
                             IID_ID3D11ShaderReflection,
                             (void **)reflection.put()))) {
      D3D11_SHADER_DESC sd{};
      if (SUCCEEDED(reflection->GetDesc(&sd))) {
        p->color_outputs_supported = true;
        for (unsigned i = 0; i < sd.OutputParameters; i++) {
          D3D11_SIGNATURE_PARAMETER_DESC param{};
          if (FAILED(reflection->GetOutputParameterDesc(i, &param)) ||
              param.SystemValueType != D3D_NAME_TARGET ||
              param.SemanticIndex > 3)
            p->color_outputs_supported = false;
          else
            p->output_mask |= 1u << param.SemanticIndex;
        }
      }
    }
    p->pso.p = (ID3D12PipelineState *)pso;
    p->pso->AddRef();
    pipeline_bytes += n + p->vertex.size();
    pipelines.emplace(pso, std::move(p));
  } catch (...) {
  }
}
extern "C" int d2_draw_pipeline_ready(void *pso) {
  if (!d2_draw_capture_enabled())
    return 0;
  auto it = pipelines.find(pso);
  return it != pipelines.end() && (it->second->color_outputs_supported &&
                                   (it->second->output_mask & 1u));
}
extern "C" void d2_draw_srv(uint32_t slot, void *resource, const void *view) {
  if (!d2_draw_capture_enabled() || !view || slot > 16384)
    return;
  try {
    auto r = (ID3D12Resource *)resource;
    if (r)
      r->AddRef();
    std::shared_ptr<ID3D12Resource> keep(r, [](ID3D12Resource *p) {
      if (p)
        p->Release();
    });
    srvs[slot] = {r, *(const D3D12_SHADER_RESOURCE_VIEW_DESC *)view,
                  std::move(keep)};
  } catch (...) {
  }
}
extern "C" int d2_draw_export(void *dp, void *qp, void *pso,
                              D2AlphaRecord record, void *user,
                              const char *folder, uint64_t frame, uint32_t draw,
                              uint32_t width, uint32_t height,
                              const void *vertices, uint32_t vertex_count,
                              const void *vc, const void *fc, uint32_t fc_bytes,
                              uint32_t first_srv, uint32_t palette_clamp,
                              void *const *borrowed, uint32_t borrowed_count) {
  if (!d2_draw_capture_enabled())
    return -1;
  if (!folder || !dp || !qp || !record || !vertices || !vc || !fc ||
      !vertex_count || vertex_count > 4096 || vertex_count % 3 || !width ||
      !height || width > 1920 || height > 1080 || fc_bytes > 16384 ||
      !fc_bytes || fc_bytes % 16 || borrowed_count > 256 ||
      (!borrowed && borrowed_count))
    return -1;
  auto ctx = std::make_unique<Context>();
  bool submitted = false, completed = false;
  std::string texture_diagnostic;
  try {
    auto found = pipelines.find(pso);
    if (found == pipelines.end())
      throw std::runtime_error("pipeline-source-unavailable");
    auto &program = *found->second;
    auto &pd = program.pd;
    if (!program.color_outputs_supported || !(program.output_mask & 1u) ||
        pd.NumRenderTargets != 1 ||
        pd.RTVFormats[0] != DXGI_FORMAT_R8G8B8A8_UNORM ||
        pd.SampleDesc.Count != 1 || pd.DepthStencilState.StencilEnable ||
        pd.RasterizerState.DepthBias ||
        pd.RasterizerState.SlopeScaledDepthBias ||
        pd.RasterizerState.DepthBiasClamp ||
        pd.BlendState.AlphaToCoverageEnable ||
        pd.BlendState.RenderTarget[0].LogicOpEnable)
      throw std::runtime_error("pipeline-outside-first-replay-scope");
    auto device = (ID3D12Device *)dp;
    auto queue = (ID3D12CommandQueue *)qp;
    for (unsigned i = 0; i < borrowed_count; i++)
      if (borrowed[i]) {
        auto p = (IUnknown *)borrowed[i];
        p->AddRef();
        ctx->borrowed.push_back(p);
      }
    d2::DrawPacket packet;
    packet.fragment_output_mask = program.output_mask;
    packet.width = width;
    packet.height = height;
    packet.vertices = vertex_count;
    packet.fragment_bytes = fc_bytes;
    auto &ds = pd.DepthStencilState;
    packet.depth_enabled = ds.DepthEnable;
    packet.depth_write = ds.DepthEnable ? ds.DepthWriteMask : 0;
    packet.depth_compare =
        ds.DepthEnable ? ds.DepthFunc : D3D12_COMPARISON_FUNC_LESS_EQUAL;
    auto &b = pd.BlendState.RenderTarget[0];
    packet.blend = b.BlendEnable;
    packet.src_color = b.SrcBlend;
    packet.dst_color = b.DestBlend;
    packet.color_op = b.BlendOp;
    packet.src_alpha = b.SrcBlendAlpha;
    packet.dst_alpha = b.DestBlendAlpha;
    packet.alpha_op = b.BlendOpAlpha;
    packet.color_mask = b.RenderTargetWriteMask;
    packet.cull = pd.RasterizerState.CullMode;
    packet.front_ccw = pd.RasterizerState.FrontCounterClockwise;
    packet.depth_clip = pd.RasterizerState.DepthClipEnable;
    packet.palette_clamp = palette_clamp;
    std::array<Srv, 4> bound{};
    for (unsigned u = 0; u < 4; u++) {
      auto it = srvs.find(first_srv + u);
      if (it == srvs.end())
        throw std::runtime_error("descriptor-unavailable");
      bound[u] = it->second;
      auto &s = bound[u];
      if (!s.resource) {
        packet.texture_width[u] = packet.texture_height[u] = 1;
        continue;
      }
      auto desc = s.resource->GetDesc();
      auto &v = s.view;
      packet.texture_format[u] = v.Format == DXGI_FORMAT_BC1_UNORM   ? 1
                                 : v.Format == DXGI_FORMAT_BC2_UNORM ? 2
                                 : v.Format == DXGI_FORMAT_BC3_UNORM ? 3
                                                                     : 0;
      texture_diagnostic =
          ",\"texture_unit\":" + std::to_string(u) +
          ",\"resource_format\":" + std::to_string(desc.Format) +
          ",\"view_format\":" + std::to_string(v.Format) +
          ",\"resource_dimension\":" + std::to_string(desc.Dimension) +
          ",\"view_dimension\":" + std::to_string(v.ViewDimension) +
          ",\"width\":" + std::to_string(desc.Width) +
          ",\"height\":" + std::to_string(desc.Height) +
          ",\"array_size\":" + std::to_string(desc.DepthOrArraySize) +
          ",\"sample_count\":" + std::to_string(desc.SampleDesc.Count) +
          ",\"resource_flags\":" + std::to_string(desc.Flags) +
          ",\"resource_mips\":" + std::to_string(desc.MipLevels) +
          ",\"view_mips\":" + std::to_string(v.Texture2D.MipLevels) +
          ",\"most_detailed_mip\":" +
          std::to_string(v.Texture2D.MostDetailedMip) + ",\"min_lod_clamp\":" +
          std::to_string(v.Texture2D.ResourceMinLODClamp) +
          ",\"component_mapping\":" + std::to_string(v.Shader4ComponentMapping);
      if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D ||
          desc.DepthOrArraySize != 1 || desc.SampleDesc.Count != 1 ||
          desc.Flags & (D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                        D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL |
                        D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) ||
          desc.Width > 4096 || desc.Height > 4096 ||
          desc.Width * desc.Height > 4194304 ||
          v.ViewDimension != D3D12_SRV_DIMENSION_TEXTURE2D ||
          v.Texture2D.MostDetailedMip || v.Texture2D.MipLevels != 1 ||
          v.Texture2D.ResourceMinLODClamp || v.Format != desc.Format ||
          (v.Format != DXGI_FORMAT_R8G8B8A8_UNORM &&
           v.Format != DXGI_FORMAT_R8_UNORM &&
           v.Format != DXGI_FORMAT_R8G8_UNORM && !packet.texture_format[u]))
        throw std::runtime_error("texture-outside-first-replay-scope");
      for (unsigned k = 0; k < 4; k++)
        if (((v.Shader4ComponentMapping >> (3 * k)) & 7) > 5)
          throw std::runtime_error("invalid-component-mapping");
      if (packet.texture_format[u])
        packet.texture_mapping[u] = v.Shader4ComponentMapping & 0xfffu;
      texture_diagnostic.clear();
      packet.texture_width[u] = (uint32_t)desc.Width;
      packet.texture_height[u] = desc.Height;
    }
    size_t payload = program.vertex.size() + program.fragment.size() +
                     size_t(width) * height * 4 + size_t(vertex_count) * 256 +
                     8224 + fc_bytes + sizeof(packet);
    for (unsigned u = 0; u < 4; u++)
      payload +=
          packet.texture_format[u]
              ? size_t((packet.texture_width[u] + 3) / 4) *
                    ((packet.texture_height[u] + 3) / 4) *
                    (packet.texture_format[u] == 1 ? 8 : 16)
              : size_t(packet.texture_width[u]) * packet.texture_height[u] * 4;
    if (payload > 268435456 - exported_bytes)
      throw std::runtime_error("session-payload-budget-exceeded");
    exported_bytes +=
        payload; // Reserve even a failed attempt's possible writes.
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_ID3D12CommandAllocator,
                                         (void **)ctx->allocator.put()));
    check(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->allocator.p, nullptr,
        IID_ID3D12GraphicsCommandList, (void **)ctx->list.put()));
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clear, IID_ID3D12Resource, (void **)ctx->color.put()));
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = desc.Format;
    clear.DepthStencil.Depth = 1;
    check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_ID3D12Resource, (void **)ctx->depth.put()));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(device->CreateDescriptorHeap(&hd, IID_ID3D12DescriptorHeap,
                                       (void **)ctx->rtv.put()));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    check(device->CreateDescriptorHeap(&hd, IID_ID3D12DescriptorHeap,
                                       (void **)ctx->dsv.put()));
    auto rt = ctx->rtv->GetCPUDescriptorHandleForHeapStart(),
         depth = ctx->dsv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(ctx->color.p, nullptr, rt);
    device->CreateDepthStencilView(ctx->depth.p, nullptr, depth);
    float black[4]{};
    ctx->list->ClearRenderTargetView(rt, black, 0, nullptr);
    ctx->list->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0,
                                     nullptr);
    ctx->list->OMSetRenderTargets(1, &rt, FALSE, &depth);
    D3D12_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    D3D12_RECT sc{0, 0, LONG(width), LONG(height)};
    ctx->list->RSSetViewports(1, &vp);
    ctx->list->RSSetScissorRects(1, &sc);
    record(ctx->list.p, user);
    auto reference =
        copy(*ctx, device, ctx->color.p, D3D12_RESOURCE_STATE_RENDER_TARGET);
    std::array<Copy, 4> copies{};
    for (unsigned u = 0; u < 4; u++)
      if (bound[u].resource)
        copies[u] = copy(*ctx, device, bound[u].resource,
                         D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    check(ctx->list->Close());
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                              (void **)ctx->fence.put()));
    ctx->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ctx->event)
      throw std::runtime_error("event-failed");
    ID3D12CommandList *lists[] = {ctx->list.p};
    queue->ExecuteCommandLists(1, lists);
    submitted = true;
    check(queue->Signal(ctx->fence.p, 1));
    check(ctx->fence->SetEventOnCompletion(1, ctx->event));
    if (WaitForSingleObject(ctx->event, 30000) != WAIT_OBJECT_0)
      throw std::runtime_error("gpu-timeout");
    completed = true;
    check(device->GetDeviceRemovedReason());
    std::string base = folder;
    auto image = rgba(reference, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    uint64_t nonzero = 0;
    for (size_t i = 0; i < image.size(); i += 4)
      nonzero += image[i] || image[i + 1] || image[i + 2] || image[i + 3];
    if (!nonzero) {
      status(
          folder,
          "{\"schema\":1,\"status\":\"empty-reference\",\"frame\":" +
              std::to_string(frame) + ",\"draw\":" + std::to_string(draw) +
              ",\"nonzero_reference_pixels\":0,\"vulkan_replayed\":false}\n");
      return 1; // Keep searching; an all-zero image is not useful evidence.
    }
    write(base + "/reference.rgba", image.data(), image.size());
    for (unsigned u = 0; u < 4; u++) {
      auto pixels = packet.texture_format[u]
                        ? blocks(copies[u], packet.texture_format[u])
                    : bound[u].resource
                        ? rgba(copies[u], bound[u].view.Shader4ComponentMapping)
                        : std::vector<uint8_t>(4, 0);
      write(base + "/tex" + std::to_string(u) +
                (packet.texture_format[u] ? ".bc" : ".rgba"),
            pixels.data(), pixels.size());
    }
    write(base + "/vp.hlsl", program.vertex.data(), program.vertex.size());
    write(base + "/fp.hlsl", program.fragment.data(), program.fragment.size());
    write(base + "/vertices.bin", vertices, size_t(vertex_count) * 256);
    write(base + "/vertex-constants.bin", vc, 8224);
    write(base + "/fragment-constants.bin", fc, fc_bytes);
    write(base + "/packet.bin", &packet, sizeof(packet));
    status(
        folder,
        "{\"schema\":1,\"status\":\"captured\",\"frame\":" +
            std::to_string(frame) + ",\"draw\":" + std::to_string(draw) +
            ",\"nonzero_reference_pixels\":" + std::to_string(nonzero) +
            ",\"shader_output_mask\":" + std::to_string(program.output_mask) +
            ",\"bound_color_targets\":1,\"discarded_shader_output_mask\":" +
            std::to_string(program.output_mask & ~1u) +
            ",\"reference\":\"isolated-original-d3d12-pipeline\",\"clear_"
            "rgba\":[0,0,0,0],\"clear_depth\":1,\"descriptor_scope\":\"fp-"
            "t0-t3\","
            "\"scene_visibility_validated\":false,\"vulkan_replayed\":false,"
            "\"normal_rendering_changed\":false}\n");
    return 0;
  } catch (const std::exception &e) {
    try {
      status(
          folder,
          std::string("{\"schema\":1,\"status\":\"rejected\",\"reason\":\"") +
              e.what() + "\"" + texture_diagnostic + "}\n");
    } catch (...) {
    }
    if (submitted && !completed)
      ctx.release();
    return -1;
  }
}

extern "C" int
d2_draw_sequence_reference(void *dp, void *qp, D2AlphaRecord record, void *user,
                           const char *folder, uint64_t frame, uint32_t width,
                           uint32_t height, const uint32_t *draws,
                           const uint32_t *first_srvs, uint32_t count,
                           void *const *borrowed, uint32_t borrowed_count) {
  if (!d2_draw_capture_enabled() || !dp || !qp || !record || !folder ||
      !draws || !first_srvs || count < 2 || count > 8 || !width || !height ||
      width > 1920 || height > 1080 || borrowed_count > 256 ||
      (!borrowed && borrowed_count))
    return -1;
  auto ctx = std::make_unique<Context>();
  bool submitted = false, completed = false;
  try {
    for (uint32_t i = 0; i < count; i++)
      if (draws[i] >= 16384 || (i && draws[i] <= draws[i - 1]))
        throw std::runtime_error("Invalid sequence order");
    if (size_t(width) * height * 4 > 268435456 - exported_bytes)
      throw std::runtime_error("session-payload-budget-exceeded");
    exported_bytes += size_t(width) * height * 4;
    // Descriptor heaps do not own the referenced resources. Retain each
    // sampled image independently, including on an unconfirmed GPU timeout.
    for (uint32_t i = 0; i < count; i++) {
      if (first_srvs[i] > 16380)
        throw std::runtime_error("Invalid sequence SRV range");
      for (unsigned u = 0; u < 4; u++) {
        auto entry = srvs.find(first_srvs[i] + u);
        if (entry == srvs.end())
          throw std::runtime_error("Sequence descriptor unavailable");
        if (entry->second.resource) {
          entry->second.resource->AddRef();
          ctx->retained.push_back(entry->second.resource);
        }
      }
    }
    auto device = (ID3D12Device *)dp;
    auto queue = (ID3D12CommandQueue *)qp;
    for (unsigned i = 0; i < borrowed_count; i++)
      if (borrowed[i]) {
        auto p = (IUnknown *)borrowed[i];
        p->AddRef();
        ctx->borrowed.push_back(p);
      }
    check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_ID3D12CommandAllocator,
                                         (void **)ctx->allocator.put()));
    check(device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, ctx->allocator.p, nullptr,
        IID_ID3D12GraphicsCommandList, (void **)ctx->list.put()));
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clear, IID_ID3D12Resource, (void **)ctx->color.put()));
    desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear.Format = desc.Format;
    clear.DepthStencil.Depth = 1;
    check(device->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear, IID_ID3D12Resource, (void **)ctx->depth.put()));
    D3D12_DESCRIPTOR_HEAP_DESC hd{};
    hd.NumDescriptors = 1;
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    check(device->CreateDescriptorHeap(&hd, IID_ID3D12DescriptorHeap,
                                       (void **)ctx->rtv.put()));
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    check(device->CreateDescriptorHeap(&hd, IID_ID3D12DescriptorHeap,
                                       (void **)ctx->dsv.put()));
    auto rt = ctx->rtv->GetCPUDescriptorHandleForHeapStart(),
         depth = ctx->dsv->GetCPUDescriptorHandleForHeapStart();
    device->CreateRenderTargetView(ctx->color.p, nullptr, rt);
    device->CreateDepthStencilView(ctx->depth.p, nullptr, depth);
    float black[4]{};
    ctx->list->ClearRenderTargetView(rt, black, 0, nullptr);
    ctx->list->ClearDepthStencilView(depth, D3D12_CLEAR_FLAG_DEPTH, 1, 0, 0,
                                     nullptr);
    ctx->list->OMSetRenderTargets(1, &rt, FALSE, &depth);
    D3D12_VIEWPORT vp{0, 0, float(width), float(height), 0, 1};
    D3D12_RECT sc{0, 0, LONG(width), LONG(height)};
    ctx->list->RSSetViewports(1, &vp);
    ctx->list->RSSetScissorRects(1, &sc);
    record(ctx->list.p, user);
    auto reference =
        copy(*ctx, device, ctx->color.p, D3D12_RESOURCE_STATE_RENDER_TARGET);
    check(ctx->list->Close());
    check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_ID3D12Fence,
                              (void **)ctx->fence.put()));
    ctx->event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ctx->event)
      throw std::runtime_error("event-failed");
    ID3D12CommandList *lists[] = {ctx->list.p};
    queue->ExecuteCommandLists(1, lists);
    submitted = true;
    check(queue->Signal(ctx->fence.p, 1));
    check(ctx->fence->SetEventOnCompletion(1, ctx->event));
    if (WaitForSingleObject(ctx->event, 30000) != WAIT_OBJECT_0)
      throw std::runtime_error("gpu-timeout");
    completed = true;
    check(device->GetDeviceRemovedReason());
    auto image = rgba(reference, D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING);
    std::string base = folder;
    write(base + "/sequence-reference.rgba", image.data(), image.size());
    d2::DrawSequence sequence;
    sequence.width = width;
    sequence.height = height;
    sequence.frame = frame;
    sequence.count = count;
    std::copy(draws, draws + count, sequence.draws);
    write(base + "/sequence.bin", &sequence, sizeof(sequence));
    const char marker[] =
        "{\"schema\":1,\"status\":\"captured\",\"purpose\":\"selected-mixed-"
        "pipeline-sequence\",\"full_frame_equivalence_tested\":false}\n";
    write(base + "/sequence.json", marker, sizeof(marker) - 1);
    return 0;
  } catch (const std::exception &e) {
    try {
      std::string marker =
          std::string("{\"status\":\"rejected\",\"reason\":\"") + e.what() +
          "\"}\n";
      write(std::string(folder) + "/sequence.json", marker.data(),
            marker.size());
    } catch (...) {
    }
    if (submitted && !completed)
      ctx.release();
    return -1;
  }
}
