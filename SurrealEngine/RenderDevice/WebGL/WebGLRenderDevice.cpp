#include "DecodeS3TC.h"
#include "WebGLRenderDevice.h"
#include "Precomp.h"
#include "UObject/ULevel.h"
#include "Window/SDL2/SDL2Window.h"
#include "Window/Window.h"
#include <SDL2/SDL.h>
#include <cstddef>

// Material equations/UV conventions adapted from SurrealEngine's GLFileResource
// and VulkanRenderDevice (zlib license; see repository LICENSE.md).
static const char *vertexSource = R"(#version 300 es
precision highp float;
layout(location=0) in vec3 p;
layout(location=1) in vec2 a;
layout(location=2) in vec2 b;
layout(location=3) in vec2 c;
layout(location=4) in vec2 d;
layout(location=5) in vec4 col;
uniform mat4 transform;
uniform bool screen;
out vec2 uv0;out vec2 uv1;out vec2 uv2;out vec2 uv3;out vec4 color;
void main(){
 gl_Position=screen?vec4(p,1.0):transform*vec4(p,1.0);
 if(!screen){gl_Position.y=-gl_Position.y;gl_Position.z=2.0*gl_Position.z-gl_Position.w;}
 uv0=a;uv1=b;uv2=c;uv3=d;color=col;
})";
static const char *fragmentSource = R"(#version 300 es
precision highp float;
precision highp int;
in vec2 uv0;in vec2 uv1;in vec2 uv2;in vec2 uv3;in vec4 color;
uniform sampler2D tex0;uniform sampler2D tex1;uniform sampler2D tex2;uniform sampler2D tex3;
uniform int flags;uniform bool masked;
out vec4 result;
vec4 dark(vec4 c){float k=3.1/255.0;return vec4(clamp((c.rgb-k)/(1.0-k),0.0,1.0),c.a);}
void main(){
 result=dark(texture(tex0,uv0))*color;
 if(masked&&result.a<0.5)discard;
 if((flags&32)!=0)result.rgb*=1.5;
 if((flags&2)!=0)result*=dark(texture(tex2,uv2));
 if((flags&1)!=0)result.rgb*=clamp(texture(tex1,uv1).rgb,0.0,1.0)*2.0;
 if((flags&4)!=0){float a=clamp(2.0-(1.0/gl_FragCoord.w)/380.0,0.0,1.0);result.rgb*=mix(vec3(1),((texture(tex3,uv3).rgb-0.5)*0.8+1.0),a);}
 else if((flags&8)!=0){vec4 f=texture(tex3,uv3);result.rgb=f.rgb+result.rgb*(1.0-f.a);}
 else if((flags&16)!=0){vec4 f=vec4(uv1,uv2);result.rgb=f.rgb+result.rgb*(1.0-f.a);}
 result=clamp(result,0.0,1.0);
})";
static GLuint compile(GLenum kind, const char *source) {
  GLuint shader = glCreateShader(kind);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[2048];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    throw std::runtime_error(log);
  }
  return shader;
}
WebGLRenderDevice::WebGLRenderDevice(GameWindow *window) {
  Viewport = window;
  auto vs = compile(GL_VERTEX_SHADER, vertexSource),
       fs = compile(GL_FRAGMENT_SHADER, fragmentSource);
  program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glLinkProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  GLint ok = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok)
    throw std::runtime_error("WebGL2 shader link failed");
  glUseProgram(program);
  matrixSlot = glGetUniformLocation(program, "transform");
  flagsSlot = glGetUniformLocation(program, "flags");
  maskSlot = glGetUniformLocation(program, "masked");
  screenSlot = glGetUniformLocation(program, "screen");
  for (int i = 0; i < 4; i++)
    glUniform1i(
        glGetUniformLocation(program, ("tex" + std::to_string(i)).c_str()), i);
  glGenVertexArrays(1, &vao);
  glBindVertexArray(vao);
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, p));
  for (int i = 0; i < 4; i++) {
    glEnableVertexAttribArray(i + 1);
    glVertexAttribPointer(i + 1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void *)(offsetof(Vertex, uv) + sizeof(vec2) * i));
  }
  glEnableVertexAttribArray(5);
  glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        (void *)offsetof(Vertex, color));
  glGenTextures(1, &white);
  glBindTexture(GL_TEXTURE_2D, white);
  uint32_t pixel = 0xffffffff;
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE,
               &pixel);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glDisable(GL_CULL_FACE);
  glDepthFunc(GL_LEQUAL);
}
WebGLRenderDevice::~WebGLRenderDevice() {
  Flush(false);
  glDeleteTextures(1, &white);
  glDeleteBuffers(1, &vbo);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
}
void WebGLRenderDevice::Flush(bool) {
  for (auto &t : textures)
    glDeleteTextures(1, &t.second);
  textures.clear();
}
void WebGLRenderDevice::Lock(vec4 scale, vec4 fog, vec4 clear) {
  flashScale = scale;
  flashFog = fog;
  SDL_GL_GetDrawableSize(SDL2Window::currentWindow, &width, &height);
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(0, 0, width, height);
  glDepthMask(GL_TRUE);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glClearColor(clear.r, clear.g, clear.b, 1);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}
void WebGLRenderDevice::Unlock(bool blit) {
  if (blit)
    SDL_GL_SwapWindow(SDL2Window::currentWindow);
}
void WebGLRenderDevice::bind(FTextureInfo *info, int unit, bool masked,
                             bool clampEdges) {
  glActiveTexture(GL_TEXTURE0 + unit);
  if (!info || !info->Mips || !info->NumMips) {
    glBindTexture(GL_TEXTURE_2D, white);
    return;
  }
  auto key = std::make_pair(info->CacheID, masked);
  auto it = textures.find(key);
  bool fresh = it == textures.end();
  GLuint id;
  if (fresh) {
    glGenTextures(1, &id);
    textures[key] = id;
  } else
    id = it->second;
  glBindTexture(GL_TEXTURE_2D, id);
  if (fresh || info->bRealtimeChanged) {
    for (int level = 0; level < info->NumMips; level++) {
      auto &mip = info->Mips[level];
      std::vector<uint8_t> rgba;
      const void *data = mip.Data.data();
      GLenum type = GL_UNSIGNED_BYTE;
      GLint internal = GL_RGBA8;
      if (info->Format == TextureFormat::P8) {
        rgba.resize((size_t)mip.Width * mip.Height * 4);
        for (size_t j = 0; j < (size_t)mip.Width * mip.Height; j++) {
          auto n = mip.Data[j];
          auto c = info->Palette[n];
          rgba[j * 4] = c.R;
          rgba[j * 4 + 1] = c.G;
          rgba[j * 4 + 2] = c.B;
          rgba[j * 4 + 3] = (masked && n == 0) ? 0 : 255;
        }
        data = rgba.data();
      } else if (info->Format == TextureFormat::BC1 || info->Format == TextureFormat::BC1_PA ||
                 info->Format == TextureFormat::BC2 || info->Format == TextureFormat::BC3) {
        rgba = DecodeS3TC(mip.Data, mip.Width, mip.Height,
                         info->Format == TextureFormat::BC2 ? 2 : info->Format == TextureFormat::BC3 ? 3 : 1);
        data = rgba.data();
      } else if (info->Format == TextureFormat::RGBA32_F) {
        type = GL_FLOAT;
        internal = GL_RGBA16F;
      } else if (info->Format == TextureFormat::BGRA8 ||
                 info->Format == TextureFormat::BGRA8_LM) {
        rgba = mip.Data;
        for (size_t j = 0; j + 3 < rgba.size(); j += 4) {
          std::swap(rgba[j], rgba[j + 2]);
          if (info->Format == TextureFormat::BGRA8_LM)
            for (int k = 0; k < 4; k++)
              rgba[j + k] = std::min(255, (int)rgba[j + k] * 2);
        }
        data = rgba.data();
      } else
        throw std::runtime_error("WebGL texture format unsupported: " +
                                 std::to_string((int)info->Format));
      glTexImage2D(GL_TEXTURE_2D, level, internal, mip.Width, mip.Height, 0,
                   GL_RGBA, type, data);
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, info->NumMips - 1);
  }
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                  info->NumMips > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                  clampEdges ? GL_CLAMP_TO_EDGE : GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                  clampEdges ? GL_CLAMP_TO_EDGE : GL_REPEAT);
}
void WebGLRenderDevice::state(uint32_t pf, bool screen) {
  if (!(pf & (PF_Translucent | PF_Modulated)))
    pf |= PF_Occlude;
  if (screen)
    glDisable(GL_DEPTH_TEST);
  else
    glEnable(GL_DEPTH_TEST);
  glDepthMask(!screen && (pf & PF_Occlude) ? GL_TRUE : GL_FALSE);
  glColorMask(!(pf & PF_Invisible), !(pf & PF_Invisible), !(pf & PF_Invisible),
              !(pf & PF_Invisible));
  glEnable(GL_BLEND);
  if (pf & PF_Translucent)
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_COLOR);
  else if (pf & PF_Modulated)
    glBlendFunc(GL_DST_COLOR, GL_SRC_COLOR);
  else if (pf & PF_Highlighted)
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  else
    glBlendFunc(GL_ONE, GL_ZERO);
}
void WebGLRenderDevice::draw(const std::vector<Vertex> &vertices, uint32_t pf,
                             int flags, bool screen, GLenum mode) {
  state(pf, screen);
  glUseProgram(program);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STREAM_DRAW);
  glUniformMatrix4fv(matrixSlot, 1, GL_FALSE, matrix.matrix);
  glUniform1i(flagsSlot, flags);
  glUniform1i(maskSlot, (pf & PF_Masked) && !(pf & PF_Translucent));
  glUniform1i(screenSlot, screen);
  glDrawArrays(mode, 0, vertices.size());
}
void WebGLRenderDevice::SetSceneNode(FSceneNode *f) {
  float z = std::tan(radians(f->FovAngle) * 0.5f), aspect = f->FY / f->FX;
  matrix = mat4::frustum(-z, z, -aspect * z, aspect * z, 1, 32768,
                         handedness::left, clipzrange::zero_positive_w) *
           f->WorldToView * f->ObjectToWorld;
  glViewport(f->XB, height - f->YB - f->Y, f->X, f->Y);
}
void WebGLRenderDevice::DrawComplexSurface(FSceneNode *f, FSurfaceInfo &s,
                                           FSurfaceFacet &facet) {
  SetSceneNode(f);
  FTextureInfo *infos[] = {s.Texture, s.LightMap, s.MacroTexture,
                           s.FogMap ? s.FogMap : s.DetailTexture};
  int flags = (s.LightMap ? 1 : 0) | (s.MacroTexture ? 2 : 0) |
              (s.FogMap          ? 8
               : s.DetailTexture ? 4
                                 : 0);
  for (int j = 0; j < 4; j++)
    bind(infos[j], j, j == 0 && (s.PolyFlags & PF_Masked),
         j == 1 || (j == 3 && s.FogMap));
  std::vector<Vertex> v(facet.VertexCount);
  for (size_t i = 0; i < v.size(); i++) {
    v[i].p = facet.Vertices[i];
    v[i].color = vec4(1);
    float u = dot(facet.MapCoords.XAxis, v[i].p - facet.MapCoords.Origin),
          w = dot(facet.MapCoords.YAxis, v[i].p - facet.MapCoords.Origin);
    for (int j = 0; j < 4; j++)
      if (infos[j]) {
        auto t = infos[j];
        float half = (j == 1 || (j == 3 && s.FogMap)) ? 0.5f : 0;
        v[i].uv[j] =
            vec2((u - t->Pan.x + half * t->UScale) / (t->UScale * t->USize),
                 (w - t->Pan.y + half * t->VScale) / (t->VScale * t->VSize));
      }
  }
  draw(v, s.PolyFlags, flags);
}
void WebGLRenderDevice::DrawGouraudPolygon(FSceneNode *f, FTextureInfo &info,
                                           const GouraudVertex *pts, int count,
                                           uint32_t pf) {
  SetSceneNode(f);
  bind(&info, 0, pf & PF_Masked);
  for (int j = 1; j < 4; j++)
    bind(nullptr, j);
  int flags = (pf & PF_Modulated) ? 0 : 32;
  if ((pf & PF_RenderFog) && !(pf & (PF_Translucent | PF_Modulated)))
    flags |= 16;
  std::vector<Vertex> v(count);
  for (int i = 0; i < count; i++) {
    v[i].p = pts[i].Point;
    v[i].uv[0] = vec2(pts[i].UV.x / (info.UScale * info.USize),
                      pts[i].UV.y / (info.VScale * info.VSize));
    v[i].color = pf & PF_Modulated ? vec4(1) : vec4(pts[i].Light, 1);
    v[i].uv[1] = vec2(pts[i].Fog.r, pts[i].Fog.g);
    v[i].uv[2] = vec2(pts[i].Fog.b, pts[i].Fog.a);
  }
  draw(v, pf, flags);
}
void WebGLRenderDevice::DrawTile(FSceneNode *f, FTextureInfo &info, float x,
                                 float y, float xl, float yl, float u, float v,
                                 float ul, float vl, float z, vec4 color,
                                 vec4 fog, uint32_t pf) {
  bind(&info, 0, pf & PF_Masked);
  for (int j = 1; j < 4; j++)
    bind(nullptr, j);
  glViewport(f->XB, height - f->YB - f->Y, f->X, f->Y);
  std::vector<Vertex> a(4);
  float xs[] = {x, x + xl, x + xl, x}, ys[] = {y, y, y + yl, y + yl};
  float us[] = {u, u + ul, u + ul, u}, vs[] = {v, v, v + vl, v + vl};
  for (int i = 0; i < 4; i++) {
    a[i].p = vec3(xs[i] / f->FX * 2 - 1, 1 - ys[i] / f->FY * 2, 0);
    a[i].uv[0] = vec2(us[i] / (info.UScale * info.USize),
                      vs[i] / (info.VScale * info.VSize));
    a[i].color = pf & PF_Modulated ? vec4(1) : vec4(color.xyz(), 1);
  }
  draw(a, pf, 0, true);
}
void WebGLRenderDevice::ClearZ(FSceneNode *) {
  glDepthMask(GL_TRUE);
  glClear(GL_DEPTH_BUFFER_BIT);
}
void WebGLRenderDevice::ReadPixels(FColor *p) {
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, p);
}
void WebGLRenderDevice::PrecacheTexture(FTextureInfo &t, uint32_t pf) {
  bind(&t, 0, pf & PF_Masked);
}
bool WebGLRenderDevice::SupportsTextureFormat(TextureFormat f) {
  return f == TextureFormat::P8 || f == TextureFormat::BGRA8 ||
         f == TextureFormat::BGRA8_LM || f == TextureFormat::RGBA32_F;
}
void WebGLRenderDevice::UpdateTextureRect(FTextureInfo &t, int, int, int, int) {
  t.bRealtimeChanged = true;
  bind(&t, 0);
}
void WebGLRenderDevice::Draw3DLine(FSceneNode *f, vec4 color, vec3 a, vec3 b) {
  SetSceneNode(f);
  for (int j = 0; j < 4; j++)
    bind(nullptr, j);
  std::vector<Vertex> v(2);
  v[0].p = a;
  v[1].p = b;
  v[0].color = v[1].color = color;
  draw(v, PF_Highlighted, 0, false, GL_LINES);
}
void WebGLRenderDevice::Draw2DLine(FSceneNode *f, vec4 color, vec3 a, vec3 b) {
  for (int j = 0; j < 4; j++)
    bind(nullptr, j);
  std::vector<Vertex> v(2);
  v[0].p = vec3(a.x / f->FX * 2 - 1, 1 - a.y / f->FY * 2, 0);
  v[1].p = vec3(b.x / f->FX * 2 - 1, 1 - b.y / f->FY * 2, 0);
  v[0].color = v[1].color = color;
  draw(v, PF_Highlighted, 0, true, GL_LINES);
}
void WebGLRenderDevice::Draw2DPoint(FSceneNode *f, vec4 color, float x, float y,
                                    float x2, float y2, float z) {
  FTextureInfo whiteInfo;
  DrawTile(f, whiteInfo, x, y, x2 - x, y2 - y, 0, 0, 1, 1, z, color, vec4(0),
           PF_Highlighted);
}
void WebGLRenderDevice::EndFlash() {
  if (flashScale == vec4(0.5f) && flashFog == vec4(0))
    return;
  for (int j = 0; j < 4; j++)
    bind(nullptr, j);
  std::vector<Vertex> v(4);
  v[0].p = vec3(-1, -1, 0);
  v[1].p = vec3(1, -1, 0);
  v[2].p = vec3(1, 1, 0);
  v[3].p = vec3(-1, 1, 0);
  float alpha = 1 - std::min(1.0f, flashScale.x * 2);
  for (auto &p : v)
    p.color = vec4(flashFog.xyz(), alpha);
  glViewport(0, 0, width, height);
  draw(v, PF_Highlighted, 0, true);
}
