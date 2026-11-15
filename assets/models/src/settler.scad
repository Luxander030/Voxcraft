// Settler: a robed, hooded villager who trades with travellers. Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 12, 0]);
vx_bone("head", "body", [0, 24, 0]);
vx_bone("arms", "body", [0, 21, 0]);
vx_bone("leg_l", "body", [-2, 12, 0]);
vx_bone("leg_r", "body", [ 2, 12, 0]);

vx_box("body", [-4, 12, -2.5], [8, 12, 5], "settler_robe");
vx_box("head", [-4, 24, -4], [8, 8, 8], "settler_hood", "settler_face");
vx_box("arms", [-4.5, 16, -5], [9, 4, 3], "settler_robe");
vx_box("arms", [-1.5, 15, -5.5], [3, 3, 2], "settler_hands");
vx_box("leg_l", [-3.5, 0, -2], [3.5, 12, 4], "settler_robe");
vx_box("leg_r", [ 0, 0, -2], [3.5, 12, 4], "settler_robe");
