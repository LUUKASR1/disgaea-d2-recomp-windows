// In-game overlay (options menu, FPS counter). Included inside namespace tr, right before submit().
//
// The overlay is a handful of textured quads drawn on top of the finished game frame with the same RSX
// draw path as the game: one font atlas texture (the last atlas cell is solid white, used for rectangles),
// one tiny pipeline, vertex colour x atlas alpha. Coordinates are output pixels with the origin at the
// top-left corner; the caller (rsx_vulkan_backend.cpp) builds the menu with ui_text / ui_rect each frame.
#include "ui_font.inc"

struct Ui {
  bool tried = false, ready = false;
  uint32_t program = 0, texture = 0;
  std::vector<RsxVertex> v;
  float W = 1.f, H = 1.f;
} ui;

inline int ui_glyph(uint32_t cp) {
  if (cp >= 32 && cp <= 126) return int(cp - 32);
  if (cp >= 160 && cp <= 255) return int(95 + cp - 160);
  return int('?' - 32);
}
// Decode the next UTF-8 code point (invalid bytes become '?').
inline uint32_t ui_next_cp(const char *&s) {
  const uint8_t c = uint8_t(*s++);
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0 && (uint8_t(*s) & 0xC0) == 0x80) return ((c & 0x1Fu) << 6) | (uint8_t(*s++) & 0x3Fu);
  if ((c & 0xF0) == 0xE0) { // 3-byte sequences are not in the atlas
    for (int k = 0; k < 2 && (uint8_t(*s) & 0xC0) == 0x80; k++) s++;
    return '?';
  }
  return '?';
}
inline void ui_begin(uint32_t w, uint32_t h) {
  ui.v.clear();
  ui.W = float(std::max(1u, w));
  ui.H = float(std::max(1u, h));
}
inline float ui_text_width(const char *s, float size) {
  float x = 0.f;
  const float k = size / float(kFontPx);
  while (*s) x += kFontAdvance[ui_glyph(ui_next_cp(s))] * k;
  return x;
}
inline float ui_line_height(float size) { return size / float(kFontPx) * float(kFontCellH); }

inline void ui_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1,
                    const std::array<float, 4> &c) {
  auto vtx = [&](float x, float y, float u, float v) {
    RsxVertex r{};
    r.attributes[0] = {x * 2.f / ui.W - 1.f, 1.f - y * 2.f / ui.H, 0.f, 1.f};
    r.attributes[3] = c;
    r.attributes[8] = {u, v, 0.f, 0.f};
    ui.v.push_back(r);
  };
  vtx(x0, y0, u0, v0); vtx(x1, y0, u1, v0); vtx(x1, y1, u1, v1);
  vtx(x0, y0, u0, v0); vtx(x1, y1, u1, v1); vtx(x0, y1, u0, v1);
}
// Solid quad with a colour per corner: p0 top-left, p1 top-right, p2 bottom-right, p3 bottom-left.
inline void ui_poly4(const float (&x)[4], const float (&y)[4], const std::array<float, 4> (&c)[4]) {
  const int white = kFontGlyphs;
  const float u = (float((white % kFontCols) * kFontCellW) + kFontCellW * 0.5f) / kFontAtlasW;
  const float v = (float((white / kFontCols) * kFontCellH) + kFontCellH * 0.5f) / kFontAtlasH;
  auto vtx = [&](int i) {
    RsxVertex r{};
    r.attributes[0] = {x[i] * 2.f / ui.W - 1.f, 1.f - y[i] * 2.f / ui.H, 0.f, 1.f};
    r.attributes[3] = c[i];
    r.attributes[8] = {u, v, 0.f, 0.f};
    ui.v.push_back(r);
  };
  vtx(0); vtx(1); vtx(2);
  vtx(0); vtx(2); vtx(3);
}
// Vertical gradient rectangle; `skew` shifts the top edge to the right (a parallelogram, for the slanted look).
inline void ui_gradient(float x, float y, float w, float h, const std::array<float, 4> &top,
                        const std::array<float, 4> &bottom, float skew = 0.f) {
  const float px[4] = {x + skew, x + w + skew, x + w, x}, py[4] = {y, y, y + h, y + h};
  const std::array<float, 4> col[4] = {top, top, bottom, bottom};
  ui_poly4(px, py, col);
}
inline void ui_diamond(float cx, float cy, float r, const std::array<float, 4> &c) {
  const float px[4] = {cx, cx + r, cx, cx - r}, py[4] = {cy - r, cy, cy + r, cy};
  const std::array<float, 4> col[4] = {c, c, c, c};
  ui_poly4(px, py, col);
}
inline void ui_rect(float x, float y, float w, float h, const std::array<float, 4> &c) {
  const int white = kFontGlyphs;
  const float u = (float((white % kFontCols) * kFontCellW) + kFontCellW * 0.5f) / kFontAtlasW;
  const float v = (float((white / kFontCols) * kFontCellH) + kFontCellH * 0.5f) / kFontAtlasH;
  ui_quad(x, y, x + w, y + h, u, v, u, v, c);
}
// Draws one line of UTF-8 (Latin-1 subset) text; (x, y) is the top-left of the line. Returns the width.
inline float ui_text(float x, float y, const char *s, float size, const std::array<float, 4> &c) {
  const float k = size / float(kFontPx);
  float pen = x;
  while (*s) {
    const int g = ui_glyph(ui_next_cp(s));
    const int col = g % kFontCols, row = g / kFontCols;
    if (g != 0) { // the space has nothing to draw
      const float cx = float(col * kFontCellW), cy = float(row * kFontCellH);
      ui_quad(pen - 2.f * k, y, pen - 2.f * k + kFontCellW * k, y + kFontCellH * k, cx / kFontAtlasW, cy / kFontAtlasH,
              (cx + kFontCellW) / kFontAtlasW, (cy + kFontCellH) / kFontAtlasH, c);
    }
    pen += kFontAdvance[g] * k;
  }
  return pen - x;
}

// Text with a dark outline (readable over any background) and optional drop shadow.
inline float ui_text_outline(float x, float y, const char *s, float size, const std::array<float, 4> &c,
                             const std::array<float, 4> &outline, float t) {
  ui_text(x + t, y + t * 1.6f, s, size, outline);
  ui_text(x - t, y - t, s, size, outline);
  ui_text(x + t, y - t, s, size, outline);
  ui_text(x - t, y + t, s, size, outline);
  return ui_text(x, y, s, size, c);
}

// One-time setup: atlas texture + overlay pipeline. Needs the render targets to exist (first submit).
bool ui_prepare(Renderer &r) {
  if (ui.ready) return true;
  if (ui.tried) return false;
  ui.tried = true;
  try {
    std::vector<uint8_t> rgba(size_t(kFontAtlasW) * kFontAtlasH * 4, 255);
    size_t p = 0;
    for (uint8_t token : kFontRle) {
      const uint8_t a = uint8_t((token >> 4) * 17);
      for (unsigned run = (token & 15u) + 1; run && p < size_t(kFontAtlasW) * kFontAtlasH; run--, p++) rgba[p * 4 + 3] = a;
    }
    ui.texture = r.createTexture(kFontAtlasW, kFontAtlasH, 0, 0x688, rgba, true, false);
    static const char *fragment =
        "struct PSInput {\n"
        "    float4 position : SV_POSITION; float4 col0 : COLOR0; float4 col1 : COLOR1;\n"
        "    float4 fog : FOG;\n"
        "    float4 tc0:TEXCOORD0; float4 tc1:TEXCOORD1; float4 tc2:TEXCOORD2; float4 tc3:TEXCOORD3;\n"
        "    float4 tc4:TEXCOORD4; float4 tc5:TEXCOORD5; float4 tc6:TEXCOORD6; float4 tc7:TEXCOORD7;\n"
        "};\n"
        "Texture2D    rsx_tex[16] : register(t0);\n"
        "SamplerState rsx_samp[16] : register(s0);\n"
        "float4 main(PSInput input) : SV_TARGET {\n"
        "    return rsx_tex[0].Sample(rsx_samp[0], input.tc0.xy) * input.col0;\n"
        "}\n";
    const auto vs = compile_cached(shader::fixtures::vertex(false, 0), shader::Stage::vertex);
    const auto fs = compile_cached(fragment, shader::Stage::fragment);
    RasterState rs;
    ui.program = r.acquireRsxPipeline(vs, fs, rs);
    ui.ready = true;
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[vulkan][ui] overlay unavailable: %.300s\n", e.what());
  }
  return ui.ready;
}

// Turns the quads collected since ui_begin() into one draw command appended after the game's draws.
void ui_flush(Renderer &r, uint32_t w, uint32_t h, std::vector<DrawCommand> &cmds) {
  if (ui.v.empty()) return;
  if (!ui_prepare(r)) { ui.v.clear(); return; }
  try {
    std::array<std::array<float, 4>, 514> vc{};
    vc[512] = {1.f, 1.f, 1.f, 1.f};
    vc[513] = {0.f, 0.f, 0.f, 0.f};
    const std::array<std::array<float, 4>, 5 + 64> fc{};
    DrawCommand d;
    d.first = uint32_t(verts.size());
    d.count = uint32_t(ui.v.size());
    d.indexed = false;
    d.depth = DepthMode::disabled;
    d.blend = true;
    d.viewport = {0, 0, w, h};
    d.scissor = d.viewport;
    d.rsx_program = ui.program;
    d.textures[0] = ui.texture;
    d.vs_block = r.frameVsConstants(vc);
    d.fp_block = r.frameFpConstants(fc);
    verts.insert(verts.end(), ui.v.begin(), ui.v.end());
    cmds.push_back(d);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[vulkan][ui] overlay draw skipped: %.200s\n", e.what());
  }
  ui.v.clear();
}

// ---- post-processing pass (internal resolution + image filters) -----------------------------------------
// Fullscreen triangle + one fragment shader reading the scene (t0/s0) and its settings from b1: pp[0] = scene
// size, output size; pp[1] = sharpen, scanlines, CRT mask, vignette; pp[2] = saturation, contrast.
inline const char *post_vertex_hlsl() {
  return "struct VSOut { float4 pos : SV_Position; float4 uv : TEXCOORD0; };\n"
         "VSOut main(uint id : SV_VertexID) {\n"
         "    VSOut o;\n"
         "    float2 p = float2(float((id << 1) & 2u), float(id & 2u));\n"
         "    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);\n"
         "    o.uv = float4(p, 0.0, 0.0);\n"
         "    return o;\n"
         "}\n";
}
inline const char *post_fragment_hlsl() {
  return "cbuffer PostParams : register(b1) { float4 pp[4]; };\n"
         "Texture2D scene : register(t0);\n"
         "SamplerState smp : register(s0);\n"
         "struct PSInput { float4 position : SV_POSITION; float4 uv : TEXCOORD0; };\n"
         "float4 main(PSInput i) : SV_TARGET {\n"
         "    float2 uv = i.uv.xy;\n"
         "    float2 texel = 1.0 / pp[0].xy;\n"
         "    float3 c = scene.SampleLevel(smp, uv, 0).rgb;\n"
         "    float sharpen = pp[1].x, scan = pp[1].y, crt = pp[1].z, vig = pp[1].w, sat = pp[2].x, con = pp[2].y;\n"
         "    if (sharpen > 0.0) {\n"
         "        float3 n = scene.SampleLevel(smp, uv + float2(0.0, -texel.y), 0).rgb;\n"
         "        float3 s = scene.SampleLevel(smp, uv + float2(0.0, texel.y), 0).rgb;\n"
         "        float3 e = scene.SampleLevel(smp, uv + float2(texel.x, 0.0), 0).rgb;\n"
         "        float3 w = scene.SampleLevel(smp, uv + float2(-texel.x, 0.0), 0).rgb;\n"
         "        float3 blur = (n + s + e + w) * 0.25;\n"
         "        c = saturate(c + (c - blur) * (sharpen * 2.0));\n"
         "    }\n"
         "    if (sat != 1.0 || con != 1.0) {\n"
         "        float l = dot(c, float3(0.299, 0.587, 0.114));\n"
         "        c = lerp(float3(l, l, l), c, sat);\n"
         "        c = saturate((c - 0.5) * con + 0.5);\n"
         "    }\n"
         "    float2 px = uv * pp[0].zw;\n"
         "    if (scan > 0.0) {\n"
         "        float period = max(2.0, pp[0].w / 360.0);\n"
         "        float lw = 0.5 + 0.5 * cos(6.2831853 * px.y / period);\n"
         "        c = c * (1.0 - scan * lw) * (1.0 + scan * 0.3);\n"
         "    }\n"
         "    if (crt > 0.0) {\n"
         "        float m = fmod(floor(px.x), 3.0);\n"
         "        float3 tri = float3(m < 0.5 ? 1.0 : 0.0, (m > 0.5 && m < 1.5) ? 1.0 : 0.0, m > 1.5 ? 1.0 : 0.0);\n"
         "        c = c * (float3(1.0, 1.0, 1.0) - crt * (float3(1.0, 1.0, 1.0) - tri)) * (1.0 + crt * 0.6);\n"
         "    }\n"
         "    if (vig > 0.0) {\n"
         "        float2 d = (uv - 0.5) * float2(1.2, 1.0);\n"
         "        c = c * (1.0 - vig * smoothstep(0.25, 0.85, length(d) * 1.6));\n"
         "    }\n"
         "    return float4(saturate(c), 1.0);\n"
         "}\n";
}
// Compiles the post shaders and hands them to the renderer. Failure only means no filters (plain blit).
void init_post(Renderer &r) {
  try {
    const auto vs = compile_cached(post_vertex_hlsl(), shader::Stage::vertex);
    const auto fs = compile_cached(post_fragment_hlsl(), shader::Stage::fragment);
    r.setPostProgram(vs, fs);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "[vulkan][ui] image filters unavailable: %.400s\n", e.what());
  }
}
