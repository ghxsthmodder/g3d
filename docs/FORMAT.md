# G3D format — initial contract

Status: draft, version 0.1.0. This document records the direction for new content; existing samples remain unchanged and may not conform.

## 1. Main document

A `.g3d` document is UTF-8 XML. The root element is `<model>`. The G3D processing instruction should appear before the root:

```xml
<?g3d version="1.0.0" encoding="utf-8"?>
<model>
  <data />
  <assets />
  <animations />
  <collisions />
  <materials />
  <mesh externalFile="src/model.mesh.g3d" />
</model>
```

The processing instruction's `version` identifies the document format version. The current parser extracts the version and performs initial structural checks; it does not yet validate every XML 1.0 rule or enforce a complete schema.

## 2. Assets and references

Assets are declared with unique, non-empty string IDs:

```xml
<assets>
  <asset id="albedo" type="texture" file="textures/albedo.ktx2" relativePath="true" />
  <asset id="surface" type="material" file="materials/surface.g3dmat" />
</assets>
```

- `id` is unique within the main document.
- `type` describes the payload (initial examples: `texture`, `shader`, `material`).
- `file` points to an external file; paths should be relative to the containing document.
- References use `assetId` and must point to a declared ID.
- Inline/base64 payloads are discouraged for large data. Binary data belongs in sidecar files.

## 3. Sidecar files

Section elements may declare `externalFile` to move their detailed data to a sidecar. The initial implementation records these paths but does not yet load or validate the contents. Relative paths should resolve from the parent `.g3d` file's directory.

Proposed sidecars include mesh geometry, asset tables, materials, animations, and collision data. Their binary layouts and extension names are **not yet stable**; do not assume compatibility until a later specification defines them.

## 4. Validation rules in the first implementation

The current C++ library checks:
- a `model` root element and balanced tags;
- basic tag/attribute syntax and quoted values;
- duplicate asset IDs;
- required `id` and `type` on `asset` elements;
- references expressed with `assetId`;
- missing/unreadable files at the CLI boundary.

The parser intentionally rejects DTD/declaration syntax and only recognizes the five predefined XML entities. It is an early structural validator, not a complete XML standard implementation.

## 5. Open design decisions

- Normative schema and compatibility policy.
- Coordinate system, units, transforms, and handedness.
- Mesh vertex/index representation and binary endianness.
- Material parameter types and texture semantics.
- Animation and collision data layout.
- Path security rules and asset-root policy.
- License for the repository as a whole.
