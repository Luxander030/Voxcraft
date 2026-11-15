// Grazer: a calm four-legged herbivore of grasslands and forests. Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 14, 0]);
vx_bone("head", "body", [0, 19, -8]);
vx_bone("leg_fl", "body", [-3, 10, -6]);
vx_bone("leg_fr", "body", [ 3, 10, -6]);
vx_bone("leg_bl", "body", [-3, 10,  6]);
vx_bone("leg_br", "body", [ 3, 10,  6]);
vx_bone("tail", "body", [0, 17, 8]);

vx_box("body", [-5, 10, -8], [10, 8, 16], "grazer_hide");
vx_box("head", [-3, 17, -15], [6, 6, 7], "grazer_hide", "grazer_face");
vx_box("head", [-3, 23, -11], [1, 3, 1], "grazer_horn");
vx_box("head", [ 2, 23, -11], [1, 3, 1], "grazer_horn");
vx_box("leg_fl", [-5, 0, -7.5], [3, 10, 3], "grazer_leg");
vx_box("leg_fr", [ 2, 0, -7.5], [3, 10, 3], "grazer_leg");
vx_box("leg_bl", [-5, 0,  4.5], [3, 10, 3], "grazer_leg");
vx_box("leg_br", [ 2, 0,  4.5], [3, 10, 3], "grazer_leg");
vx_box("tail", [-1, 13, 8], [2, 5, 2], "grazer_hide");
