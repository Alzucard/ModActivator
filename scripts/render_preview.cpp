// Draws what ModActivatorPreview exported (the menu's Dear ImGui draw lists
// and font atlas) into raw RGBA pixels, the way a GPU would: textured,
// coloured, alpha-blended triangles inside their clip rectangles, over a
// dark backdrop. scripts/render_preview.py builds and runs it and turns the
// result into PNGs.
//
//   render_preview <preview.bin> <out.rgba> <width> <height>
//
// The file layout is ModActivatorPreview's: atlas width, height and RGBA
// pixels; then per draw list its vertices (ImDrawVert: pos, uv, colour),
// 32-bit indices, and commands (element count, index offset, vertex offset,
// clip rectangle).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Vert {
  float x, y, u, v;
  uint32_t col; // IM_COL32: red in the low byte
};

static bool get(FILE *f, void *p, size_t n) { return fread(p, 1, n, f) == n; }
template <typename T> static bool get(FILE *f, T &v) { return get(f, &v, sizeof v); }

int main(int argc, char **argv) {
  if (argc != 5) {
    fprintf(stderr, "usage: render_preview <preview.bin> <out.rgba> <width> <height>\n");
    return 2;
  }
  const int width = atoi(argv[3]), height = atoi(argv[4]);
  FILE *f = fopen(argv[1], "rb");
  if (!f)
    return 1;
  int tw = 0, th = 0;
  get(f, tw);
  get(f, th);
  std::vector<uint8_t> atlas(size_t(tw) * th * 4);
  get(f, atlas.data(), atlas.size());

  // A dark backdrop, a little lighter at the top, as a game scene behind
  // the panel would read.
  std::vector<float> img(size_t(width) * height * 3);
  for (int y = 0; y < height; ++y)
    for (int x = 0; x < width; ++x) {
      const float t = float(y) / height;
      float *p = &img[(size_t(y) * width + x) * 3];
      p[0] = 0.085f - 0.04f * t;
      p[1] = 0.090f - 0.04f * t;
      p[2] = 0.110f - 0.05f * t;
    }

  const auto sample = [&](float u, float v, float out[4]) {
    // Bilinear, as the overlay's sampler is.
    const float fx = u * tw - 0.5f, fy = v * th - 0.5f;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float ax = fx - x0, ay = fy - y0;
    for (int c = 0; c < 4; ++c)
      out[c] = 0;
    for (int j = 0; j < 2; ++j)
      for (int i = 0; i < 2; ++i) {
        const int sx = std::clamp(x0 + i, 0, tw - 1), sy = std::clamp(y0 + j, 0, th - 1);
        const float w = (i ? ax : 1 - ax) * (j ? ay : 1 - ay);
        const uint8_t *t = &atlas[(size_t(sy) * tw + sx) * 4];
        for (int c = 0; c < 4; ++c)
          out[c] += w * t[c] / 255.0f;
      }
  };

  // What was drawn, for cropping: printed at the end as "x0 y0 x1 y1".
  float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
  int lists = 0;
  get(f, lists);
  for (int l = 0; l < lists; ++l) {
    int nv = 0;
    get(f, nv);
    std::vector<Vert> verts(nv);
    get(f, verts.data(), sizeof(Vert) * nv);
    for (const auto &v : verts) {
      minX = std::min(minX, v.x);
      minY = std::min(minY, v.y);
      maxX = std::max(maxX, v.x);
      maxY = std::max(maxY, v.y);
    }
    int ni = 0;
    get(f, ni);
    std::vector<uint32_t> idx(ni);
    get(f, idx.data(), sizeof(uint32_t) * ni);
    int nc = 0;
    get(f, nc);
    for (int c = 0; c < nc; ++c) {
      uint32_t count = 0, idxOffset = 0, vtxOffset = 0;
      float clip[4];
      get(f, count);
      get(f, idxOffset);
      get(f, vtxOffset);
      get(f, clip, sizeof clip);
      const int cx0 = std::max(0, int(clip[0])), cy0 = std::max(0, int(clip[1]));
      const int cx1 = std::min(width, int(std::ceil(clip[2]))), cy1 = std::min(height, int(std::ceil(clip[3])));
      for (uint32_t t = 0; t + 2 < count; t += 3) {
        const Vert &a = verts[vtxOffset + idx[idxOffset + t]];
        const Vert &b = verts[vtxOffset + idx[idxOffset + t + 1]];
        const Vert &d = verts[vtxOffset + idx[idxOffset + t + 2]];
        const float area = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
        if (std::fabs(area) < 1e-6f)
          continue;
        const int x0 = std::max(cx0, int(std::floor(std::min({a.x, b.x, d.x}))));
        const int y0 = std::max(cy0, int(std::floor(std::min({a.y, b.y, d.y}))));
        const int x1 = std::min(cx1, int(std::ceil(std::max({a.x, b.x, d.x}))));
        const int y1 = std::min(cy1, int(std::ceil(std::max({a.y, b.y, d.y}))));
        for (int y = y0; y < y1; ++y)
          for (int x = x0; x < x1; ++x) {
            const float px = x + 0.5f, py = y + 0.5f;
            const float w0 = ((b.x - px) * (d.y - py) - (b.y - py) * (d.x - px)) / area;
            const float w1 = ((d.x - px) * (a.y - py) - (d.y - py) * (a.x - px)) / area;
            const float w2 = 1 - w0 - w1;
            if (w0 < -1e-5f || w1 < -1e-5f || w2 < -1e-5f)
              continue;
            float tex[4];
            sample(w0 * a.u + w1 * b.u + w2 * d.u, w0 * a.v + w1 * b.v + w2 * d.v, tex);
            float col[4];
            for (int k = 0; k < 4; ++k)
              col[k] = (w0 * ((a.col >> (8 * k)) & 255) + w1 * ((b.col >> (8 * k)) & 255) +
                        w2 * ((d.col >> (8 * k)) & 255)) / 255.0f;
            const float alpha = col[3] * tex[3];
            float *p = &img[(size_t(y) * width + x) * 3];
            for (int k = 0; k < 3; ++k)
              p[k] = col[k] * tex[k] * alpha + p[k] * (1 - alpha);
          }
      }
    }
  }
  fclose(f);

  FILE *out = fopen(argv[2], "wb");
  if (!out)
    return 1;
  std::vector<uint8_t> rgba(size_t(width) * height * 4);
  for (size_t i = 0; i < size_t(width) * height; ++i) {
    for (int k = 0; k < 3; ++k)
      rgba[i * 4 + k] = uint8_t(std::clamp(img[i * 3 + k], 0.0f, 1.0f) * 255.0f + 0.5f);
    rgba[i * 4 + 3] = 255;
  }
  fwrite(rgba.data(), 1, rgba.size(), out);
  fclose(out);
  printf("%d %d %d %d\n", std::max(0, int(minX)), std::max(0, int(minY)),
         std::min(width, int(std::ceil(maxX))), std::min(height, int(std::ceil(maxY))));
  return 0;
}
