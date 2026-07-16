#include "common.h"
#include "windowman.h"
#include "renderman.h"
#include "contentman.h"
#include "gameman.h"

using namespace strikers;

int main()
{
    windowman wman{};
    window_create_args window_args{};
    window_args.m_title = "rastergraph";
    window_args.m_size = { 640, 480 };
    window_id wid = wman.new_window(window_args).claim();

    // initialize content
    contentman cman{};
    cman.scan_assets_in_folder(k_content_folder);
    
    // initialize the renderer
    renderman rman{};
    rman.initialize().claim();
    rman.compile_shaders();

    gameman gman{};
    gman.start();

    const platform::window_handle wplatform_handle = wman.get_window_platform_handle(wid).claim();
    rman.register_window(wplatform_handle).claim();

    float time = 0.0f;
    while (true)
    {
        const float delta_time = 0.1f;
        time += delta_time;

        wman.poll_windows();
        gman.tick_input();
        gman.tick(time, delta_time);

        renderscene scene;
        gman.build_renderscene(scene);
        rman.render(scene, cman);
    }
}