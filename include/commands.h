#pragma once
#include "commandman.h"

#ifndef DF_COMMANDS
#define DF_COMMANDS
#define INLINE_CMD inline

namespace strikers {
// game commands
INLINE_CMD command cm_camera_sensy("camera_sensy", "0.1");
INLINE_CMD command cm_camera_dev_control("camera_dev_control", "0");
INLINE_CMD command cm_camera_dev_reset("camera_dev_reset", "r");
INLINE_CMD command cm_camera_dev_left("camera_dev_left", "a");
INLINE_CMD command cm_camera_dev_right("camera_dev_right", "d");
INLINE_CMD command cm_camera_dev_forward("camera_dev_forward", "w");
INLINE_CMD command cm_camera_dev_back("camera_dev_back", "s");
INLINE_CMD command cm_camera_dev_up("camera_dev_up", "space");
INLINE_CMD command cm_camera_dev_down("camera_dev_down", "shift");
INLINE_CMD command cm_camera_dev_sprint("camera_dev_sprint", "shift");
INLINE_CMD command cm_camera_maxspeed("camera_maxspeed", "10");
INLINE_CMD command cm_camera_acceleration("camera_acceleration", "10");

// game controls
INLINE_CMD command p0_cm_controls_left("controls_left_p0", "a");
INLINE_CMD command p0_cm_controls_up("controls_up_p0", "w");
INLINE_CMD command p0_cm_controls_right("controls_right_p0", "d");
INLINE_CMD command p0_cm_controls_down("controls_down_p0", "s");
INLINE_CMD command p0_cm_controls_pass("controls_pass_p0", "shift");
INLINE_CMD command p0_cm_controls_shoot("controls_shoot_p0", "space");

INLINE_CMD command p1_cm_controls_left("controls_left_p1", "left");
INLINE_CMD command p1_cm_controls_up("controls_up_p1", "up");
INLINE_CMD command p1_cm_controls_right("controls_right_p1", "right");
INLINE_CMD command p1_cm_controls_down("controls_down_p1", "down");
INLINE_CMD command p1_cm_controls_pass("controls_pass_p1", "shift");
INLINE_CMD command p1_cm_controls_shoot("controls_shoot_p1", "space");

// physics
INLINE_CMD command cm_phx_enabled("phx_enabled", "1");
INLINE_CMD command cm_phx_gravity_enabled("phx_gravity_enabled", "1");

// game
INLINE_CMD command cm_game_reset("game_reset", "k");
INLINE_CMD command cm_game_airdrag("game_airdrag", "1.225");
INLINE_CMD command cm_game_movement_acc("game_movement_acc", "2");
INLINE_CMD command cm_game_movement_mxsp("game_movement_mxsp", "6");
INLINE_CMD command cm_game_ball_mxsp("game_ball_mxsp", "1000");
INLINE_CMD command cm_game_ball_pass_spd("game_ball_pass_spd", "1");
INLINE_CMD command cm_game_ball_shot_spd("game_ball_shot_spd", "1");
INLINE_CMD command cm_game_ball_decay_seconds("game_ball_decay_seconds", "8"); // time it takes for a ball to decay entirely to 0
INLINE_CMD command cm_game_ball_maxshoot_seconds("game_ball_maxshoot_seconds", "2"); // time until shoot releases
INLINE_CMD command cm_game_ball_maxcharge_seconds("game_ball_maxcharge_seconds", "4"); // time until charge is full
INLINE_CMD command cm_game_ai("game_ai", "1");
INLINE_CMD command cm_game_ai_min_dtime("game_ai_min_dtime", "1");
INLINE_CMD command cm_game_ai_max_dtime("game_ai_max_dtime", "1");
INLINE_CMD command cm_game_ai_random("game_ai_random", "1");
INLINE_CMD command cm_game_camera_speed("game_camera_speed", "1");
INLINE_CMD command cm_game_camera_ball_offset_x("game_camera_ball_offset_x", "0");
INLINE_CMD command cm_game_camera_ball_offset_z("game_camera_ball_offset_z", "23");
INLINE_CMD command cm_draw_dbg("draw_dbg", "0");
INLINE_CMD command cm_draw_dbg_bounds("draw_dbg_bounds", "0");
INLINE_CMD command cm_draw_dbg_transforms("draw_dbg_transforms", "0");
INLINE_CMD command cm_draw_dbg_axis("draw_dbg_axis", "0");
INLINE_CMD command cm_draw_dbg_ball("draw_dbg_ball", "0");

// animation
INLINE_CMD command cm_anim_speed("cm_anim_speed", "1");
INLINE_CMD command cm_gfx_shadowmap_size("gfx_shadowmap_size", "1024");
INLINE_CMD command cm_gfx_shadowfrustrum_size("gfx_shadowfrustrum_size", "50");

// ui
INLINE_CMD command cm_ui_enabled("ui_enabled", "1");
INLINE_CMD command cm_ui_alpha("ui_alpha", "1");
INLINE_CMD command cm_ui_scale("ui_scale", "1");

// dbg_ui
INLINE_CMD command cm_ui_dbg_enabled("ui_dbg_enabled", "1");
INLINE_CMD command cm_ui_dbg_runner_idx("ui_dbg_runner_idx", "0");
INLINE_CMD command cm_ui_dbg_runner_team("ui_dbg_runner_team", "0");

// logging
INLINE_CMD command cm_log_cmds("log_commands", "0", command::flags::oneshot);

// test
INLINE_CMD command cm_test_text("test_text", "abcd");
}
#endif DF_COMMANDS