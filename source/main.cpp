#include "common.h"
#include "windowman.h"
#include "renderman.h"
#include "contentman.h"
#include "gameman.h"
#include "inputman.h"
#include "commandman.h"
using namespace strikers;

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\"; }

command cm_log_fps("log_fps", "1");
command cm_log_clear("log_clear", "0", command::flags::oneshot);

int main()
{
    windowman wman{};
    window_create_args window_args{};
    window_args.m_title = "strikers";
    window_args.m_size = { 1280, 720 };
    window_id wid = wman.new_window(window_args).claim();

    // initialize content
    contentman cman{};
    // cman.scan_assets_in_folder(k_content_folder);
    cman.load_fbx(string(k_content_folder) + "scene.fbx").claim();
    cman.load_fbx(string(k_content_folder) + "meshes/box.fbx").claim();

    // initialize the renderer
    renderman rman{};
    rman.initialize().claim();
    rman.compile_shaders();

    gameman gman{};
    gman.start(cman);

    const platform::window_handle wplatform_handle = wman.get_window_platform_handle(wid).claim();
    rman.register_window(wplatform_handle).claim();

    using timepoint = std::chrono::steady_clock::time_point;
    timepoint last = std::chrono::steady_clock::now();
    timepoint now = last;
    float time = 0.0f;
    while (true)
    {   
        now = std::chrono::steady_clock::now();
        const float delta_seconds = std::chrono::duration<float>(now - last).count();
        last = now;
        time += delta_seconds;
        
        inputman::get().tick();
        commandman::get().tick();
        wman.poll_windows();

        gman.tick(time, delta_seconds);

        renderscene scene;
        gman.build_renderscene(cman, scene);
        rman.render(scene, cman);

        if (cm_log_fps.get_value() > 0)
        {
            const float fps = 1.0f / delta_seconds;
            if (fps > 30) logman::color(logcolor::green, 1);
            logman::log("[fps] {}", 1.0f / delta_seconds);
        }

        if (cm_log_clear.get_value() > 0)
        {
            system("cls");
        }
    }
}