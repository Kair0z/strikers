# camera
camera.dev.control  1;
camera.dev.reset    r;
camera.dev.left     a;
camera.dev.right    d;
camera.dev.forward  w;
camera.dev.back     s;
camera.dev.up       space;
camera.dev.down     shift;

camera.sensy        0.1;
camera.maxspeed     500;
camera.acceleration 100000;

# logging
log.fps 1;
log.clear 0;
log.commands 0;

# game
game.debug.reset;
game.physics.enable 0;
game.gravity.enable 0;
game.airdensity 1.225;

# physics
phys.debug.disable 1;
phys.timestep 10;

# draw
draw.debug 1;
draw.debug.physics 0;