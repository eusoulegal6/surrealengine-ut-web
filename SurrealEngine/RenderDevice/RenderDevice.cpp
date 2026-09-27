
#include "Precomp.h"
#include "RenderDevice.h"
#include "Vulkan/VulkanRenderDevice.h"
#ifdef __EMSCRIPTEN__
#include "WebGL/WebGLRenderDevice.h"
#else
#include "OpenGL/OpenGLRenderDevice.h"
#endif

#include <iostream>

std::unique_ptr<RenderDevice> RenderDevice::Create(GameWindow* viewport)
{
	#ifdef __EMSCRIPTEN__
 return std::make_unique<WebGLRenderDevice>(viewport);
#else
 return std::make_unique<OpenGLRenderDevice>(viewport);
#endif
}

std::unique_ptr<RenderDevice> RenderDevice::CreateUnused(GameWindow* viewport, std::shared_ptr<VulkanSurface> surface) {
	#ifdef __EMSCRIPTEN__
 return {};
#else
 return std::make_unique<VulkanRenderDevice>(viewport, surface);
#endif
}
