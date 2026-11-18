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
  std::vector<uint32_t> inputs;   // attribute locations the overlay vertex shader reads (its compact slot order)
  std::vector<RsxVertex> v;       // quads are built with the legacy 16-attribute layout and packed in ui_flush
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
    ui.inputs = vertex_inputs(vs);
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
    const size_t S = std::max<size_t>(1, ui.inputs.size());
    const size_t vbase = (verts.size() + S - 1) / S * S;   // a draw starts at a multiple of its stride
    d.first = uint32_t(indices.size());
    d.count = uint32_t(ui.v.size());
    d.indexed = true;
    d.depth = DepthMode::disabled;
    d.blend = true;
    d.viewport = {0, 0, w, h};
    d.scissor = d.viewport;
    d.rsx_program = ui.program;
    d.textures[0] = ui.texture;
    d.vs_block = r.frameVsConstants(vc);
    d.fp_block = r.frameFpConstants(fc);
    if (vbase + ui.v.size() * S > kMaxSlots || indices.size() + ui.v.size() > kMaxIndices) throw std::runtime_error("frame is full");
    verts.resize(vbase + ui.v.size() * S);
    for (size_t k = 0; k < ui.v.size(); k++) {
      Slot *out = &verts[vbase + k * S];
      for (size_t i = 0; i < S; i++) out[i] = i < ui.inputs.size() ? ui.v[k].attributes[ui.inputs[i] & 15u] : Slot{};
      indices.push_back(uint32_t(vbase / S + k));
    }
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
// FSR 1 (EASU + RCAS) below is a port of AMD FidelityFX Super Resolution 1.0 (ffx_fsr1.h), MIT License,
// Copyright (c) 2021 Advanced Micro Devices, Inc. Full licence text: docs/THIRD-PARTY.md.
// FXAA is the public "FXAA lite" edge-direction blur (Timothy Lottes).
inline const char *post_fragment_hlsl() {
  return
      "cbuffer PostParams : register(b1) { float4 pp[4]; };\n"
      "Texture2D scene : register(t0);\n"
      "SamplerState smp : register(s0);\n"
      "struct PSInput { float4 position : SV_POSITION; float4 uv : TEXCOORD0; };\n"
      // ---------------------------------------------------------------- EASU
      "void easuTap(inout float3 aC, inout float aW, float2 off, float2 dir, float2 len, float lob, float clp, float3 c) {\n"
      "    float2 v = float2(off.x * dir.x + off.y * dir.y, off.x * (-dir.y) + off.y * dir.x);\n"
      "    v *= len;\n"
      "    float d2 = min(v.x * v.x + v.y * v.y, clp);\n"
      "    float wB = 0.4 * d2 - 1.0;\n"
      "    float wA = lob * d2 - 1.0;\n"
      "    wB *= wB; wA *= wA;\n"
      "    wB = 1.5625 * wB - 0.5625;\n"
      "    float w = wB * wA;\n"
      "    aC += c * w; aW += w;\n"
      "}\n"
      "void easuSet(inout float2 dir, inout float len, float2 q, int which, float lA, float lB, float lC, float lD, float lE) {\n"
      "    float w = 0.0;\n"
      "    if (which == 0) w = (1.0 - q.x) * (1.0 - q.y);\n"
      "    if (which == 1) w = q.x * (1.0 - q.y);\n"
      "    if (which == 2) w = (1.0 - q.x) * q.y;\n"
      "    if (which == 3) w = q.x * q.y;\n"
      "    float lenX = max(abs(lD - lC), abs(lC - lB));\n"
      "    lenX = 1.0 / max(lenX, 0.00001);\n"
      "    float dirX = lD - lB;\n"
      "    dir.x += dirX * w;\n"
      "    lenX = saturate(abs(dirX) * lenX);\n"
      "    lenX *= lenX;\n"
      "    len += lenX * w;\n"
      "    float lenY = max(abs(lE - lC), abs(lC - lA));\n"
      "    lenY = 1.0 / max(lenY, 0.00001);\n"
      "    float dirY = lE - lA;\n"
      "    dir.y += dirY * w;\n"
      "    lenY = saturate(abs(dirY) * lenY);\n"
      "    lenY *= lenY;\n"
      "    len += lenY * w;\n"
      "}\n"
      "float3 easu(float2 ip) {\n"
      "    float2 ratio = pp[0].xy / pp[0].zw;\n"
      "    float2 q = ip * ratio + (0.5 * ratio - 0.5);\n"
      "    float2 fp = floor(q);\n"
      "    q -= fp;\n"
      "    float2 isz = 1.0 / pp[0].xy;\n"
      "    float2 p0 = fp * isz + float2(isz.x, -isz.y);\n"
      "    float2 p1 = p0 + float2(-isz.x, 2.0 * isz.y);\n"
      "    float2 p2 = p0 + float2(isz.x, 2.0 * isz.y);\n"
      "    float2 p3 = p0 + float2(0.0, 4.0 * isz.y);\n"
      "    float4 bczzR = scene.GatherRed(smp, p0), bczzG = scene.GatherGreen(smp, p0), bczzB = scene.GatherBlue(smp, p0);\n"
      "    float4 ijfeR = scene.GatherRed(smp, p1), ijfeG = scene.GatherGreen(smp, p1), ijfeB = scene.GatherBlue(smp, p1);\n"
      "    float4 klhgR = scene.GatherRed(smp, p2), klhgG = scene.GatherGreen(smp, p2), klhgB = scene.GatherBlue(smp, p2);\n"
      "    float4 zzonR = scene.GatherRed(smp, p3), zzonG = scene.GatherGreen(smp, p3), zzonB = scene.GatherBlue(smp, p3);\n"
      "    float4 bczzL = bczzB * 0.5 + (bczzR * 0.5 + bczzG);\n"
      "    float4 ijfeL = ijfeB * 0.5 + (ijfeR * 0.5 + ijfeG);\n"
      "    float4 klhgL = klhgB * 0.5 + (klhgR * 0.5 + klhgG);\n"
      "    float4 zzonL = zzonB * 0.5 + (zzonR * 0.5 + zzonG);\n"
      "    float bL = bczzL.x, cL = bczzL.y, iL = ijfeL.x, jL = ijfeL.y, fL = ijfeL.z, eL = ijfeL.w;\n"
      "    float kL = klhgL.x, lL = klhgL.y, hL = klhgL.z, gL = klhgL.w, oL = zzonL.z, nL = zzonL.w;\n"
      "    float2 dir = float2(0.0, 0.0);\n"
      "    float len = 0.0;\n"
      "    easuSet(dir, len, q, 0, bL, eL, fL, gL, jL);\n"
      "    easuSet(dir, len, q, 1, cL, fL, gL, hL, kL);\n"
      "    easuSet(dir, len, q, 2, fL, iL, jL, kL, nL);\n"
      "    easuSet(dir, len, q, 3, gL, jL, kL, lL, oL);\n"
      "    float dirR = dir.x * dir.x + dir.y * dir.y;\n"
      "    bool zro = dirR < (1.0 / 32768.0);\n"
      "    dirR = rsqrt(max(dirR, 1.0 / 32768.0));\n"
      "    dirR = zro ? 1.0 : dirR;\n"
      "    dir.x = zro ? 1.0 : dir.x;\n"
      "    dir *= dirR;\n"
      "    len = len * 0.5;\n"
      "    len *= len;\n"
      "    float stretch = (dir.x * dir.x + dir.y * dir.y) / max(max(abs(dir.x), abs(dir.y)), 0.00001);\n"
      "    float2 len2 = float2(1.0 + (stretch - 1.0) * len, 1.0 - 0.5 * len);\n"
      "    float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;\n"
      "    float clp = 1.0 / lob;\n"
      "    float3 f3 = float3(ijfeR.z, ijfeG.z, ijfeB.z), g3 = float3(klhgR.w, klhgG.w, klhgB.w);\n"
      "    float3 j3 = float3(ijfeR.y, ijfeG.y, ijfeB.y), k3 = float3(klhgR.x, klhgG.x, klhgB.x);\n"
      "    float3 min4 = min(min(min(f3, g3), j3), k3);\n"
      "    float3 max4 = max(max(max(f3, g3), j3), k3);\n"
      "    float3 aC = float3(0.0, 0.0, 0.0);\n"
      "    float aW = 0.0;\n"
      "    easuTap(aC, aW, float2( 0.0, -1.0) - q, dir, len2, lob, clp, float3(bczzR.x, bczzG.x, bczzB.x));\n"
      "    easuTap(aC, aW, float2( 1.0, -1.0) - q, dir, len2, lob, clp, float3(bczzR.y, bczzG.y, bczzB.y));\n"
      "    easuTap(aC, aW, float2(-1.0,  1.0) - q, dir, len2, lob, clp, float3(ijfeR.x, ijfeG.x, ijfeB.x));\n"
      "    easuTap(aC, aW, float2( 0.0,  1.0) - q, dir, len2, lob, clp, j3);\n"
      "    easuTap(aC, aW, float2( 0.0,  0.0) - q, dir, len2, lob, clp, f3);\n"
      "    easuTap(aC, aW, float2(-1.0,  0.0) - q, dir, len2, lob, clp, float3(ijfeR.w, ijfeG.w, ijfeB.w));\n"
      "    easuTap(aC, aW, float2( 1.0,  1.0) - q, dir, len2, lob, clp, k3);\n"
      "    easuTap(aC, aW, float2( 2.0,  1.0) - q, dir, len2, lob, clp, float3(klhgR.y, klhgG.y, klhgB.y));\n"
      "    easuTap(aC, aW, float2( 2.0,  0.0) - q, dir, len2, lob, clp, float3(klhgR.z, klhgG.z, klhgB.z));\n"
      "    easuTap(aC, aW, float2( 1.0,  0.0) - q, dir, len2, lob, clp, g3);\n"
      "    easuTap(aC, aW, float2( 1.0,  2.0) - q, dir, len2, lob, clp, float3(zzonR.z, zzonG.z, zzonB.z));\n"
      "    easuTap(aC, aW, float2( 0.0,  2.0) - q, dir, len2, lob, clp, float3(zzonR.w, zzonG.w, zzonB.w));\n"
      "    return min(max4, max(min4, aC * (1.0 / aW)));\n"
      "}\n"
      // Input of RCAS: EASU when upscaling, plain bilinear otherwise.
      "float3 fsrSrc(float2 ip) {\n"
      "    ip = clamp(ip, float2(0.0, 0.0), pp[0].zw - 1.0);\n"
      "    if (pp[0].x <= pp[0].z + 0.5) return easu(ip);\n"
      "    return scene.SampleLevel(smp, (ip + 0.5) / pp[0].zw, 0).rgb;\n"
      "}\n"
      // ---------------------------------------------------------------- RCAS
      "float3 rcas(float2 ip, float sharp) {\n"
      "    float3 b = fsrSrc(ip + float2(0.0, -1.0));\n"
      "    float3 d = fsrSrc(ip + float2(-1.0, 0.0));\n"
      "    float3 e = fsrSrc(ip);\n"
      "    float3 f = fsrSrc(ip + float2(1.0, 0.0));\n"
      "    float3 h = fsrSrc(ip + float2(0.0, 1.0));\n"
      "    float3 mn4 = min(min(min(b, d), f), h);\n"
      "    float3 mx4 = max(max(max(b, d), f), h);\n"
      "    float3 hitMin = min(mn4, e) / (4.0 * max(mx4, 0.0001));\n"
      "    float3 hitMax = (1.0 - max(mx4, e)) / min(4.0 * mn4 - 4.0, -0.0001);\n"
      "    float3 lobeRGB = max(-hitMin, hitMax);\n"
      "    float lobe = max(-(0.25 - 1.0 / 16.0), min(max(max(lobeRGB.r, lobeRGB.g), lobeRGB.b), 0.0)) * sharp;\n"
      "    float rcpL = 1.0 / (4.0 * lobe + 1.0);\n"
      "    return (lobe * b + lobe * d + lobe * h + lobe * f + e) * rcpL;\n"
      "}\n"
      // ---------------------------------------------------------------- FXAA (lite)
      "float3 fxaa(float2 uv, float2 texel) {\n"
      "    float3 lw = float3(0.299, 0.587, 0.114);\n"
      "    float3 rgbNW = scene.SampleLevel(smp, uv + float2(-1.0, -1.0) * texel, 0).rgb;\n"
      "    float3 rgbNE = scene.SampleLevel(smp, uv + float2( 1.0, -1.0) * texel, 0).rgb;\n"
      "    float3 rgbSW = scene.SampleLevel(smp, uv + float2(-1.0,  1.0) * texel, 0).rgb;\n"
      "    float3 rgbSE = scene.SampleLevel(smp, uv + float2( 1.0,  1.0) * texel, 0).rgb;\n"
      "    float3 rgbM = scene.SampleLevel(smp, uv, 0).rgb;\n"
      "    float lNW = dot(rgbNW, lw), lNE = dot(rgbNE, lw), lSW = dot(rgbSW, lw), lSE = dot(rgbSE, lw), lM = dot(rgbM, lw);\n"
      "    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));\n"
      "    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));\n"
      "    float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), (lNW + lSW) - (lNE + lSE));\n"
      "    float dirReduce = max((lNW + lNE + lSW + lSE) * (0.25 * (1.0 / 8.0)), 1.0 / 128.0);\n"
      "    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);\n"
      "    dir = clamp(dir * rcpDirMin, -8.0, 8.0) * texel;\n"
      "    float3 rgbA = 0.5 * (scene.SampleLevel(smp, uv + dir * (1.0 / 3.0 - 0.5), 0).rgb + scene.SampleLevel(smp, uv + dir * (2.0 / 3.0 - 0.5), 0).rgb);\n"
      "    float3 rgbB = rgbA * 0.5 + 0.25 * (scene.SampleLevel(smp, uv + dir * -0.5, 0).rgb + scene.SampleLevel(smp, uv + dir * 0.5, 0).rgb);\n"
      "    float lB = dot(rgbB, lw);\n"
      "    return (lB < lMin || lB > lMax) ? rgbA : rgbB;\n"
      "}\n"
      "float4 main(PSInput i) : SV_TARGET {\n"
      "    float2 uv = i.uv.xy;\n"
      "    float2 texel = 1.0 / pp[0].xy;\n"
      "    float sharpen = pp[1].x, scan = pp[1].y, crt = pp[1].z, vig = pp[1].w, sat = pp[2].x, con = pp[2].y;\n"
      "    float aa = pp[2].z, fsr = pp[2].w;\n"
      "    float3 c;\n"
      "    if (fsr > 0.0) {\n"
      "        c = saturate(rcas(floor(uv * pp[0].zw), fsr));\n"
      "    } else {\n"
      "        c = (aa > 0.0) ? fxaa(uv, texel) : scene.SampleLevel(smp, uv, 0).rgb;\n"
      "        if (sharpen > 0.0) {\n"
      "            float3 n = scene.SampleLevel(smp, uv + float2(0.0, -texel.y), 0).rgb;\n"
      "            float3 s = scene.SampleLevel(smp, uv + float2(0.0, texel.y), 0).rgb;\n"
      "            float3 e = scene.SampleLevel(smp, uv + float2(texel.x, 0.0), 0).rgb;\n"
      "            float3 w = scene.SampleLevel(smp, uv + float2(-texel.x, 0.0), 0).rgb;\n"
      "            float3 blur = (n + s + e + w) * 0.25;\n"
      "            c = saturate(c + (c - blur) * (sharpen * 2.0));\n"
      "        }\n"
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
