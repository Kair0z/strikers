# camera
camera_dev_control  0;
camera_dev_reset    o;
camera_dev_left     a;
camera_dev_right    d;
camera_dev_forward  w;
camera_dev_back     s;
camera_dev_up       space;
camera_dev_down     shift;

camera_sensy        0.1;
camera_maxspeed     1000;
camera_acceleration 100000000;

# logging
log_fps 0;
log_clear 0;
log_commands 0;

# game
game_reset r;
game_physics_enable 1;
game_gravity_enable 0;
game_airdrag 1.225;
game_movement_acc 1000000;
game_movement_mxsp 1000;

game_ai 1;
game_ai_min_dtime 0.1;
game_ai_max_dtime 1.1;
game_ai_random 0.0000000001;

# physics
phys_debug_disable 1;
phys_timestep 10;

# draw
draw_debug 0;
draw_debug_phys1cs 0;