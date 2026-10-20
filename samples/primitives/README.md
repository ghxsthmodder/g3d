# G3D v1.0 primitive reference samples

These self-contained XML samples target the proposed G3D v1.0 format (root element `<g3d version="1.0">`). Each file contains one geometry, one primitive, one simple metallic-roughness material, and one scene root node.

| File | Shape | Notes |
|---|---|---|
| [triangle.g3d](triangle.g3d) | Triangle | Three vertices; lies on the XZ plane. |
| [square.g3d](square.g3d) | Square | Two triangles; lies on the XZ plane. |
| [sphere.g3d](sphere.g3d) | Sphere | UV sphere, 12 slices × 6 stacks. |
| [cylinder.g3d](cylinder.g3d) | Cylinder | Closed cylinder, 16 radial segments, with separate side/cap normals. |
| [torus.g3d](torus.g3d) | Torus | Major radius 1, minor radius 0.35, tessellation 16 × 8. |
| [flat.g3d](flat.g3d) | Flat plane (“Is Flat”) | Rectangular, single flat surface on the XZ plane. |

## Conventions

- Right-handed coordinates: +Y is up; +Z is forward.
- Distances are in meters.
- UV origin is **top-left**, with +V pointing down, matching the v1.0 canonical
  image orientation. `v' = 1 - v` is the flip from the bottom-up convention used
  by OpenGL textures.
- Geometry uses a shared index stream for positions, normals, and UV0.
- `count` counts entries (vertices for vertex streams; scalar indices for `indices`), not individual numeric components.
- Polygon topology is triangles; face winding is counter-clockwise when viewed from the front.
- The `flat.g3d` file interprets “Is Flat” as a flat plane/floor primitive.

## Compatibility note

These samples are self-contained authoring documents: ASCII coordinate arrays, no
`<pool>`, no sidecar `.bin` and no `.ktx2`. That is the authoring mode of v1.0 and it
stays valid — a published package is what has to move to the binary pool and KTX2.

The repository parser validates them (`g3d-tool validate`). It checks structure —
root, version, section order, IDs and reference resolution — not semantics: it does
not verify that `uv0` has as many entries as `positions`, that indices stay inside
the vertex range, or that the UVs follow the top-left origin above.
