// Gloomling: a hunched night creature with lantern eyes; hates daylight. Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 12, 0]);
vx_bone("head", "body", [0, 22, -1]);
vx_bone("arm_l", "body", [-5.5, 21, 0]);
vx_bone("arm_r", "body", [ 5.5, 21, 0]);
vx_bone("leg_l", "body", [-2, 12, 0]);
vx_bone("leg_r", "body", [ 2, 12, 0]);

vx_box("body", [-4, 12, -2.5], [8, 10, 5], "gloomling_skin");
vx_box("head", [-3.5, 22, -4.5], [7, 7, 7], "gloomling_skin", "gloomling_face");
vx_box("arm_l", [-7, 9, -1.5], [3, 13, 3], "gloomling_skin");
vx_box("arm_r", [ 4, 9, -1.5], [3, 13, 3], "gloomling_skin");
vx_box("leg_l", [-3.5, 0, -1.5], [3, 12, 3], "gloomling_leg");
vx_box("leg_r", [ 0.5, 0, -1.5], [3, 12, 3], "gloomling_leg");
