#pragma once
#include "RenderDevice/RenderDevice.h"
#include <GLES3/gl3.h>
#include <map>
#include <vector>

// UT web fork: WebGL2 adapter consuming original SurrealEngine draw data.
class WebGLRenderDevice final : public RenderDevice {
public:
 explicit WebGLRenderDevice(GameWindow* window);
 ~WebGLRenderDevice() override;
 void Flush(bool) override;
 void Lock(vec4,vec4,vec4) override;
 void Unlock(bool) override;
 void DrawComplexSurface(FSceneNode*,FSurfaceInfo&,FSurfaceFacet&) override;
 void DrawGouraudPolygon(FSceneNode*,FTextureInfo&,const GouraudVertex*,int,uint32_t) override;
 void DrawTile(FSceneNode*,FTextureInfo&,float,float,float,float,float,float,float,float,float,vec4,vec4,uint32_t) override;
 void Draw3DLine(FSceneNode*,vec4,vec3,vec3) override;
 void Draw2DLine(FSceneNode*,vec4,vec3,vec3) override;
 void Draw2DPoint(FSceneNode*,vec4,float,float,float,float,float) override;
 void ClearZ(FSceneNode*) override;
 void ReadPixels(FColor*) override;
 void EndFlash() override;
 void SetSceneNode(FSceneNode*) override;
 void PrecacheTexture(FTextureInfo&,uint32_t) override;
 bool SupportsTextureFormat(TextureFormat) override;
 void UpdateTextureRect(FTextureInfo&,int,int,int,int) override;
private:
 struct Vertex {vec3 p; vec2 uv[4]; vec4 color;};
 GLuint program=0,vbo=0,vao=0,white=0;
 GLint matrixSlot,flagsSlot,maskSlot,screenSlot;
 std::map<std::pair<uint64_t,bool>,GLuint> textures;
 mat4 matrix=mat4::identity();
 vec4 flashScale=vec4(0.5f),flashFog=vec4(0);
 int width=1,height=1;
 void bind(FTextureInfo*,int,bool=false,bool=false);
 void draw(const std::vector<Vertex>&,uint32_t,int,bool=false,GLenum=GL_TRIANGLE_FAN);
 void state(uint32_t,bool);
};
