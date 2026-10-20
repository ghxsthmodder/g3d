# G3D format specification

**Version:** 1.0 · **Status:** draft · **Extension:** `.g3d` · **Encoding:** UTF-8 XML 1.0

This document defines the intended G3D v1.0 text format. The C++ parser currently implements only a subset of these rules; it is not a complete XML processor or schema validator.

## 1. Main document

A document has exactly one `<g3d version="1.0">` root. The version is an attribute on the root, not a G3D processing instruction.

```xml
<?xml version="1.0" encoding="UTF-8"?>
<g3d version="1.0">
  <mesh>
    <geometry id="triangle">
      <positions count="3">0 0 0  1 0 0  0 1 0</positions>
      <indices count="3" topology="triangles">0 1 2</indices>
    </geometry>
    <primitive id="triangle-primitive" geometry="triangle" material="default"/>
  </mesh>
  <materials>
    <material id="default" name="Default">
      <pbr workflow="metallicRoughness">
        <baseColor r="0.8" g="0.8" b="0.8" a="1"/>
        <metallic value="0"/><roughness value="0.8"/>
      </pbr>
    </material>
  </materials>
  <scene><node id="root"><mesh ref="triangle-primitive"/></node></scene>
</g3d>
```

Top-level sections occur at most once and in order: `metadata`, `assets`, `mesh`, `materials`, `animations`, `collisions`, `scene`. The scene is required and contains exactly one root node. Whitespace and comments do not affect semantics. DOCTYPE and external entities are prohibited; parsers must not fetch network resources.

## 2. IDs and references

IDs are case-sensitive and should match `[A-Za-z_][A-Za-z0-9_.-]*`. IDs are unique within each category across the model and auxiliaries; different categories may reuse the same text. Names are descriptive, not references.

- `primitive/@geometry` → geometry ID
- `primitive/@material` → material ID
- `node/mesh/@ref` → primitive ID
- `channel/@target` → node ID
- `body/@node` → node ID
- mesh collision shape geometry → geometry ID
- texture slot → texture asset ID

All references must resolve. Duplicate IDs within a category and unresolved references are errors.

## 3. Auxiliary files and paths

A section can be inline or external via `src`, never both. External documents use their section as root (e.g. `<mesh>...</mesh>`) and inherit the main file's version. Paths are relative to the main `.g3d` directory. Absolute paths, URLs, and paths escaping the authorized package are prohibited. Recursive auxiliary references are unsupported in v1.0. Asset files use the same path policy.

The parser currently records section `src` paths but does not load auxiliary files or enforce path containment.

## 4. Coordinates and transforms

- Right-handed coordinates: +X right, +Y up, +Z forward.
- Distances are meters; angles are radians; positive rotations follow the right-hand rule.
- Numeric arrays are whitespace-separated, use a dot decimal separator, and may use scientific notation. Commas, NaN, and infinities are invalid.
- Column vectors: `M_local = T * R * S`; `M_global = M_parent * M_local`. A root's global transform equals its local transform.
- Quaternion order is XYZW; normalize within tolerance `1e-4`.
- Missing transform components mean identity. Scale components must be finite and nonzero; negative scale is allowed.
- Arbitrary matrices and shear are unsupported. `<transform>` children occur in order `translation`, `rotation`, `scale`, each at most once.

## 5. Geometry and primitives

A mesh section contains reusable geometries and primitives. Each primitive binds one geometry to one material; geometries may be reused.

Each geometry has an ID and exactly one positions stream and one indices stream. Optional streams are normals, tangents, uv0, uv1, and colors0; each occurs at most once. Positions contain at least three XYZ vertices. Every present vertex stream has the same entry count as positions. `count` counts entries, not scalar components. Indices are shared by all streams, in range `[0, positions.count)`, number at least three, and have a count divisible by three. Triangle topology is the only v1.0 topology; winding is counter-clockwise viewed from the front. Duplicate vertices are needed at UV seams or hard normal edges.

All values must be finite. Normals are normalized. Tangents use normalized XYZ and W equal to -1 or +1. Colors are RGBA in 0..1. UVs may be outside 0..1.

## 6. Materials and textures

Materials use metallic-roughness PBR. Base color is RGBA in 0..1; metallic and roughness are each in 0..1. Every primitive must reference a material; no implicit default material exists.

Texture semantics are base color, normal, metallic, roughness, occlusion, and emissive. References must resolve to texture assets. PNG support is mandatory; JPEG optional. KTX2 and DDS are extensions, not mandatory in v1.0. Base-color and emissive textures use sRGB; normal, metallic, roughness, and occlusion use linear interpretation. Normal maps use the OpenGL-style tangent-space convention (positive green = positive tangent-space Y).

## 7. Animations

Animations have unique IDs and finite positive duration in seconds. Channels target existing nodes and animate translation, rotation, or scale. Interpolation is `step` or `linear`; linear rotation uses SLERP. A target/property pair cannot repeat within one animation. Keyframe times are finite, non-negative, strictly increasing, and no greater than duration. Translation/scale use 3 components; rotation uses 4 XYZW components. The first key should normally be at time zero. Cubic spline, morph weights, and ticks-per-second are unsupported in v1.0.

## 8. Collision data

Bodies reference scene nodes and have type static, dynamic, or kinematic. Shapes are box, sphere, capsule, cylinder, or mesh. Box dimensions are full positive extents; sphere radius is positive; capsule radius and cylindrical-section height are positive with axis +Y; cylinder radius and height are positive with axis +Y. Mesh shapes use `staticTriangleMesh` or `convex`; static triangle meshes require static bodies and dynamic triangle meshes are unsupported. Shape-local translation/rotation are optional; local scale is unsupported. Mass, friction, restitution, and other engine-specific properties are outside the mandatory v1.0 contract.

## 9. Validation and security

A conforming validator rejects malformed XML, unsupported versions, unknown elements/attributes, duplicate IDs within a category, unresolved references, invalid numeric values, incorrect array lengths, out-of-range indices, invalid transforms, and invalid body/shape combinations. Schema validation alone cannot enforce cross-references, hierarchy cycles, path containment, quaternion norms, or all count constraints.

Implementations should limit XML size, nesting, array lengths and counts; check integer overflow before allocation; never execute scripts or load arbitrary libraries from a model.

## 10. Current parser limitations

The dependency-free parser checks the root/version, basic tag balancing and entity syntax, top-level section order/duplicates, required `scene`, IDs on key records, and selected geometry/material/primitive/node references. It records external `src` paths but does not load auxiliaries.

It is not yet fully conformant: complete XML 1.0 handling, unknown nested element/attribute rejection, array-count/index validation, exact scene-root cardinality, hierarchy cycles, path containment, and complete material/animation/collision semantics remain future work. A successful parse is not proof that every semantic requirement passed.

## 11. Reference samples

`samples/primitives/` contains Triangle, Square, Sphere, Cylinder, Torus, and Flat Plane examples. These are fixtures, not a guarantee that every semantic rule is already enforced.
