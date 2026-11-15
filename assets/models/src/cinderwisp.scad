// Cinderwisp: a floating ember spirit of the Hollow; keeps its distance and hurls ember bolts.
// Original Voxcraft creature.
include <lib.scad>

vx_bone("body", "", [0, 10, 0]);
vx_bone("shard_a", "body", [0, 10, 0]);
vx_bone("shard_b", "body", [0, 10, 0]);
vx_bone("shard_c", "body", [0, 10, 0]);
vx_bone("head", "body", [0, 10, 0]);

vx_box("head", [-3, 7, -3], [6, 6, 6], "cinderwisp_core", "cinderwisp_face");
vx_box("shard_a", [5, 9, -1], [2, 2, 2], "cinderwisp_shard");
vx_box("shard_b", [-7, 11, -1], [2, 2, 2], "cinderwisp_shard");
vx_box("shard_c", [-1, 3, 5], [2, 2, 2], "cinderwisp_shard");
vx_box("body", [-2, 5, -2], [4, 2, 4], "cinderwisp_shard");
