// Cart: an open riveted iron tub on four small wheels. Original Voxcraft model.
// Faces -Z (forward). 14 px wide, 18 px long, rides 1 px above the rail.
include <lib.scad>

vx_bone("body", "", [0, 0, 0]);

vx_box("body", [-7, 2, -9], [14, 1, 18], "cart_metal");           // floor
vx_box("body", [-7, 3, -9], [14, 6, 1], "cart_metal");            // front wall
vx_box("body", [-7, 3, 8], [14, 6, 1], "cart_metal");             // back wall
vx_box("body", [-7, 3, -8], [1, 6, 16], "cart_metal");            // left wall
vx_box("body", [6, 3, -8], [1, 6, 16], "cart_metal");             // right wall
vx_box("body", [-6, 0, -7], [2, 2, 3], "cart_wheel");
vx_box("body", [4, 0, -7], [2, 2, 3], "cart_wheel");
vx_box("body", [-6, 0, 4], [2, 2, 3], "cart_wheel");
vx_box("body", [4, 0, 4], [2, 2, 3], "cart_wheel");
