// Puffbird: a round, flightless-ish bird that flutters when startled. Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 5, 0]);
vx_bone("head", "body", [0, 9, -2]);
vx_bone("wing_l", "body", [-3, 8, 0]);
vx_bone("wing_r", "body", [ 3, 8, 0]);
vx_bone("leg_l", "body", [-1.5, 3, 0]);
vx_bone("leg_r", "body", [ 1.5, 3, 0]);

vx_box("body", [-3, 3, -3], [6, 6, 7], "puffbird_feathers");
vx_box("head", [-2, 8, -5], [4, 4, 4], "puffbird_feathers", "puffbird_face");
vx_box("head", [-1, 9, -7], [2, 1, 2], "puffbird_beak");
vx_box("wing_l", [-4, 4, -2], [1, 4, 5], "puffbird_wing");
vx_box("wing_r", [ 3, 4, -2], [1, 4, 5], "puffbird_wing");
vx_box("leg_l", [-2, 0, -1], [1, 3, 1], "puffbird_beak");
vx_box("leg_r", [ 1, 0, -1], [1, 3, 1], "puffbird_beak");
