// Rockmite: a small armoured cave critter that swarms intruders. Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 3, 0]);
vx_bone("head", "body", [0, 3, -5]);
vx_bone("leg_fl", "body", [-4, 2, -3]);
vx_bone("leg_fr", "body", [ 4, 2, -3]);
vx_bone("leg_bl", "body", [-4, 2,  3]);
vx_bone("leg_br", "body", [ 4, 2,  3]);

vx_box("body", [-4, 1, -5], [8, 4, 10], "rockmite_shell");
vx_box("head", [-2.5, 1, -9], [5, 3, 4], "rockmite_shell", "rockmite_face");
vx_box("leg_fl", [-6, 0, -3.5], [2, 2, 1], "rockmite_leg");
vx_box("leg_fr", [ 4, 0, -3.5], [2, 2, 1], "rockmite_leg");
vx_box("leg_bl", [-6, 0,  2.5], [2, 2, 1], "rockmite_leg");
vx_box("leg_br", [ 4, 0,  2.5], [2, 2, 1], "rockmite_leg");
