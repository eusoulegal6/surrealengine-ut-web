
#include "Precomp.h"
#include "Exception.h"
#include "GameApp.h"
#include "CommandLine.h"
#include "GameFolder.h"
#include "Engine.h"
#include "UI/WidgetResourceData.h"
#include "File.h"
#include <stdexcept>
#include <zwidget/core/theme.h>
#include <zwidget/window/window.h>
#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;

#if __EMSCRIPTEN__
#include <emscripten.h>
#include "UObject/UClient.h"
#include "UObject/UActor.h"
Engine *EMSCRIPTEN_GLOBAL_GAME_ENGINE = nullptr;
// Browser controls enter the same original key/axis binding system as SDL.
extern "C" {
EMSCRIPTEN_KEEPALIVE void web_key(int key, int down) {
    if (!EMSCRIPTEN_GLOBAL_GAME_ENGINE || key < 0 || key > 255) return;
    auto* e = EMSCRIPTEN_GLOBAL_GAME_ENGINE;
    if (down) e->OnWindowKeyDown(static_cast<EInputKey>(key));
    else e->OnWindowKeyUp(static_cast<EInputKey>(key));
}
EMSCRIPTEN_KEEPALIVE void web_look(int dx, int dy) {
    if (EMSCRIPTEN_GLOBAL_GAME_ENGINE) EMSCRIPTEN_GLOBAL_GAME_ENGINE->OnWindowRawMouseMove(dx,dy);
}
EMSCRIPTEN_KEEPALIVE void web_focus(int active) {
    if (!EMSCRIPTEN_GLOBAL_GAME_ENGINE) return;
    if (active) EMSCRIPTEN_GLOBAL_GAME_ENGINE->OnWindowActivated();
    else EMSCRIPTEN_GLOBAL_GAME_ENGINE->OnWindowDeactivated();
}
}
void emscripten_game_loop_step() {
	try {
        EMSCRIPTEN_GLOBAL_GAME_ENGINE->Run();
        static int frames = 0;
        if (++frames % 15 == 0 && EMSCRIPTEN_GLOBAL_GAME_ENGINE->viewport && EMSCRIPTEN_GLOBAL_GAME_ENGINE->viewport->Actor()) {
            auto* player = EMSCRIPTEN_GLOBAL_GAME_ENGINE->viewport->Actor();
            auto p = player->Location(); auto v = player->Velocity(); auto r = player->ViewRotation();
            EM_ASM({
                var el=document.getElementById('telemetry');
                var now=performance.now();var fps=Module.utLastSample?15000/(now-Module.utLastSample):0;Module.utLastSample=now;
                if(el)el.textContent='Frame '+$0+' | '+fps.toFixed(1)+' fps'+' | position '+Math.round($1)+','+Math.round($2)+','+Math.round($3)+' | velocity '+Math.round($4)+','+Math.round($5)+','+Math.round($6)+' | view '+$7+','+$8+' | health '+$9;
            }, frames, p.x,p.y,p.z,v.x,v.y,v.z,r.Pitch,r.Yaw,player->Health());
        }
    } catch (const std::exception& e) {
        emscripten_cancel_main_loop();
        std::cerr << e.what() << std::endl;
        EM_ASM({Module['onAbort'](UTF8ToString($0));}, e.what());
    }
}
#endif

int GameApp::main(std::vector<std::string> args)
{

	{
    	std::string path = "."; // current directory

    	try {
        	for (const auto & entry : fs::directory_iterator(path)) {
            	std::cout << entry.path().filename().string() << std::endl;
        	}
    	} catch (const fs::filesystem_error& e) {
        	std::cerr << "Error: " << e.what() << std::endl;
    	}
	}

	std::cout << "GameApp main" << std::endl;	

	std::cout << "DisplayBackend::TryCreateSDL2()" << std::endl;	
	auto backend = DisplayBackend::TryCreateSDL2();
	std::cout << "DisplayBackend::Set(std::move(backend))" << std::endl;
	DisplayBackend::Set(std::move(backend));
	std::cout << "InitWidgetResources()" << std::endl;
	InitWidgetResources();
	std::cout << "WidgetTheme::SetTheme" << std::endl;
	WidgetTheme::SetTheme(std::make_unique<DarkWidgetTheme>());

	std::cout << "Args" << std::endl;

	static CommandLine cmd(args);
	commandline = &cmd;

	GameLaunchInfo info = GameFolderSelection::GetLaunchInfo();

#ifdef EMSCRIPTEN
		EMSCRIPTEN_GLOBAL_GAME_ENGINE = new Engine(info);
		emscripten_set_main_loop(emscripten_game_loop_step, 0, 0);
#else
	if (!info.gameRootFolder.empty())
	{
		std::cout << "Engine" << std::endl;
		Engine engine(info);
		std::cout << "Run Engine" << std::endl;
		engine.Run();
	}

	DeinitWidgetResources();
#endif
	return 0;
}
