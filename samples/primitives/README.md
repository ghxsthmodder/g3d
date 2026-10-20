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
- Geometry uses a shared index stream for positions, normals, and UV0.
- `count` counts entries (vertices for vertex streams; scalar indices for `indices`), not individual numeric components.
- Polygon topology is triangles; face winding is counter-clockwise when viewed from the front.
- The `flat.g3d` file interprets “Is Flat” as a flat plane/floor primitive.

## Compatibility note

These are reference examples for the evolving v1.0 specification. The repository's experimental C++ parser currently targets an earlier `<model>`-root draft and is not expected to validate these `<g3d>` documents until that parser is updated to the finalized specification.
