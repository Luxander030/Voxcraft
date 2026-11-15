// Wanderer: the player character, seen in third-person views. Original Voxcraft character:
// a traveller in a teal tunic with a satchel strap, cropped dark hair and sturdy boots.
include <lib.scad>

vx_bone("body", "", [0, 12, 0]);
vx_bone("head", "body", [0, 24, 0]);
vx_bone("arm_l", "body", [-6, 22, 0]);
vx_bone("arm_r", "body", [ 6, 22, 0]);
vx_bone("leg_l", "body", [-2, 12, 0]);
vx_bone("leg_r", "body", [ 2, 12, 0]);

vx_box("body", [-4, 12, -2], [8, 12, 4], "wanderer_tunic", "wanderer_tunic_front");
vx_box("head", [-4, 24, -4], [8, 8, 8], "wanderer_hair", "wanderer_face");
vx_box("arm_l", [-8, 10, -2], [4, 12, 4], "wanderer_sleeve");
vx_box("arm_r", [ 4, 10, -2], [4, 12, 4], "wanderer_sleeve");
vx_box("leg_l", [-4, 0, -2], [4, 12, 4], "wanderer_trousers");
vx_box("leg_r", [ 0, 0, -2], [4, 12, 4], "wanderer_trousers");
