// Voxcraft box-model library (OpenSCAD).
//
// Conventions: 1 unit = 1 texture pixel = 1/16 block. Origin = entity feet centre. +Y up,
// the model faces -Z (forward). Pivots are in model space.
//
// vx_bone() and vx_box() both produce geometry (so the model can be previewed/rendered and its
// geometry validated) AND echo a machine-readable record that tools/scad_export.py turns into
// the runtime box model. The .scad files are the single editable source of truth.

// Declare a bone: name, parent ("" = root), pivot point the bone rotates around.
module vx_bone(name, parent, pivot) {
    echo("VXBONE", name, parent, pivot);
    %translate(pivot) sphere(0.5, $fn = 6);   // preview-only marker
}

// A textured box attached to a bone. tex: texture for all faces; front: texture for the -Z face.
module vx_box(bone, min, size, tex, front = "") {
    echo("VXBOX", bone, min, size, tex, front == "" ? tex : front);
    translate(min) cube(size);
}
