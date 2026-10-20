# G3D v1.0 canonical sample — `character.g3d`

This model is the specification driver for G3D v1.0: it deliberately exercises
every block the format defines, so that each block has to have concrete syntax
instead of prose. Read it together with [docs/FORMAT.md](../../docs/FORMAT.md)
(structure and semantics) and [docs/BINARY.md](../../docs/BINARY.md) (pool and
`.g3dp` envelope).

## What it covers

| Block | Element | Where |
|---|---|---|
| Metadata | `<metadata>` with `createdAt` in ISO-8601 and `<license src>` | top of file |
| Textures | `<asset type="texture">` pointing at `.ktx2` only; `kind` omitted, so `2d` | `<assets>` |
| Variants | `<textureSet>` with ASTC / ETC2 / RGBA8 candidates and a mandatory `default` | `<assets>` |
| Atlas array | `<asset kind="2dArray">` sampled through `<texture layer="1">` | `<assets>`, `<materials>` |
| Skin | `<skeleton>`, `<joint parent>`, `<inverseBindMatrices>`, `<skin>/<bind>` | `<skeletons>` |
| Binary pool | `<pool>`, `<buffer>`, `<bufferView>`, `view=` + `byteOffset=` streams | `<mesh>` |
| Interleaving | one view with `byteStride="36"` shared by seven streams | `geometry/body` |
| Tight packing | `byteStride="0"` views | `geometry/face` |
| Quantization | `uint16` positions with affine `<quantization>` | `geometry/body` |
| Normalisation | `int8` normals/tangents, `uint16` UVs and weights | both geometries |
| Morph | `<morph>` with three dense targets on a 128-vertex geometry | `geometry/face` |
| Texture slots | `<texture assetId texCoord layer colorspace channel…>`, `<transform>`, `strength`, `scale` | `<materials>` |
| Alpha | `alphaMode`, `premultiplied`, `doubleSided` | `<material>` |
| Camera | `<camera type="perspective">` attached to a node | `<cameras>`, `<scene>` |
| Light | `<light type="spot">` with cone angles and range | `<lights>`, `<scene>` |
| Animation | `<sampler>/<input>/<output>`, `channel/@target` on a **node** and on a **primitive** | `<animations>` |
| Collision | `<body type="dynamic">` with a `<shape type="capsule">` | `<collisions>` |

`geometry/body` is skinned (`joints0` + `weights0` + `primitive/@skin`) and
`geometry/face` is morph-only and rigidly bound. That split is deliberate, not an
oversight: the rule is `skin` ⇔ `joints0`/`weights0`, so one geometry shows the
skinned case and the other shows morph without skin. A document where every primitive
is both would exercise neither boundary.

## Files

Present:

- `character.g3d` — the document above.
- `LICENSE` — the file `<metadata><license src="LICENSE"/>` points at, resolved
  under the §5 path policy (a package-relative path; `../LICENSE` would escape it).

Referenced but **not** in the repository yet:

- `character.bin` — the 21 248-byte pool described below.
- `textures/*.ktx2` — eight KTX2 images (three albedo variants, two ORM, one
  normal, two emissive). The two emissive files are `kind="2dArray"` and must carry the
  same `layerCount` (§8.3 rules 5 and 6); the other six are `2d`.

These are deliberately absent. Fabricating them would mean writing a packer and
a KTX2 encoder, which is the next step of the project, not this one; a placeholder
file would also pass the structural checks while teaching readers that the pool
layout is arbitrary. The tables below are the real contract: they are computed by
hand so the byte arithmetic can be reviewed now and checked mechanically once the
packer exists.

## Pool layout

All `byteOffset` values of a `<buffer>` are absolute in the pool; all
`byteOffset` values of a `<bufferView>` are relative to its `@buffer`; the
`byteOffset` of a stream is relative to the start of its view (i.e. an offset
inside `byteStride` when the view is interleaved). Absolute offset of one element
of a stream is therefore:

```
pool.buffer.byteOffset + view.byteOffset + stream.byteOffset + index * (view.byteStride ? view.byteStride : elementSize)
```

### Buffers

| buffer | pool offset | length | end | align | note |
|---|---|---|---|---|---|
| `buf_static` | 0 | 15 160 | 15 160 | 256 | vertex and index data |
| `buf_morph` | 15 168 | 5 760 | 20 928 | 16 | starts at the next 16-byte boundary; 15 160 is not a multiple of 16 |
| `buf_skin` | 20 928 | 320 | 21 248 | 16 | inverse bind matrices |

Padding bytes 15 160 … 15 167 (8 bytes) are zero-filled. Pool length 21 248 =
1 328 × 16.

### Views

| view | buffer | offset in buffer | length | stride | math |
|---|---|---|---|---|---|
| `v_body_verts` | `buf_static` | 0 | 9 216 | 36 | 256 vertices × 36 |
| `v_face_pos` | `buf_static` | 9 216 | 1 536 | 0 | 128 × 3 × float32 |
| `v_face_nrm` | `buf_static` | 10 752 | 384 | 0 | 128 × 3 × int8 |
| `v_face_uv0` | `buf_static` | 11 136 | 1 024 | 0 | 128 × 2 × float32 |
| `v_body_idx` | `buf_static` | 12 160 | 1 200 | 0 | 600 × uint16 |
| `v_face_idx` | `buf_static` | 13 360 | 1 800 | 0 | 900 × uint16 |
| `v_morph_pos` | `buf_morph` | 0 | 4 608 | 0 | 3 targets × 128 verts × 3 comps × 4 |
| `v_morph_nrm` | `buf_morph` | 4 608 | 1 152 | 0 | 3 × 128 × 3 comps × 1 |
| `v_ibm` | `buf_skin` | 0 | 320 | 0 | 5 joints × 16 comps × 4 |

`v_face_idx` ends at 13 360 + 1 800 = 15 160, exactly `buf_static.byteLength`, and
the view offsets are contiguous, so no view overlaps another. Both
`elementArrayBuffer` views start on a multiple of 16 and all others on a multiple of
4, as the alignment table requires. For the interleaved
view the containment rule is `(count − 1) × byteStride + byteOffset + elementSize ≤
byteLength`; the tightest stream is `weights0`: 255 × 36 + 28 + 8 = 9 216, which is
the view length exactly.

### Vertex layout of `v_body_verts` (`byteStride="36"`)

| offset | size | stream | componentType | normalized | components |
|---|---|---|---|---|---|
| 0 | 6 | `positions` | `uint16` | no (affine quantization) | 3 |
| 6 | 2 | — | reserved padding, zero-filled | | |
| 8 | 3 | `normals` | `int8` | yes, symmetric | 3 |
| 11 | 1 | — | reserved padding, zero-filled | | |
| 12 | 4 | `tangents` | `int8` | yes, symmetric | 4 (w = ±127 → ±1) |
| 16 | 4 | `uv0` | `uint16` | yes | 2 |
| 20 | 4 | `uv1` | `uint16` | yes | 2 |
| 24 | 4 | `joints0` | `uint8` | no | 4 |
| 28 | 8 | `weights0` | `uint16` | yes | 4 |

The stride is a multiple of 4 and every attribute starts on a boundary of its own
component size, both of which are required. `tangents.w` is stored as ±127 because
symmetric normalization maps it to exactly ±1, which is the tangent-sign rule.
`uv0`/`uv1` as normalized `uint16` can only express [0, 1]; `geometry/face`
therefore uses `float32` UVs to show the mode that allows wrapped or repeated
coordinates.

## Quantization of `body.positions`

Decode rule for a signed quantized attribute:

```
q          value stored as uint16, range [0, 65535]
component  c = q / 65535                        (unsigned, so c spans exactly [0, 1])
position   p = offset + c * scale
```

Because `min` is the `offset` and `max` is `offset + scale`, quantization is an
affine map over the whole bounding box and the storage must be **unsigned**;
signed symmetric normalization is reserved for direction attributes such as
normals and tangents, where the value itself lives in [-1, 1].

With `offset="-0.6 -0.02 -0.35"` and `scale="1.2 0.88 0.7"`: `q = 0` decodes to
the min corner `-0.6 -0.02 -0.35`, `q = 32768` decodes to approximately the centre
`0.000008 0.420006 0.000005`, and `q = 65535` decodes to `0.6 0.86 0.35`, which
equals `offset + scale` and matches the declared `max`. Precision is
scale / 65535 ≈ 0.011 mm to 0.018 mm per axis, against 12 bytes per vertex for the
same data as `float32`.

## Constraints the sample must satisfy

| constraint | value in the sample |
|---|---|
| every `view=` resolves | 9 views, all declared before use |
| every stream `count` equals `positions.count` of its geometry | 256 (body), 128 (face) |
| morph dense storage | `targetPositions.count` = `morph/@count` × `positions.count` = 3 × 128 = 384 |
| `inverseBindMatrices/@count` = number of joints | 5 = 5 |
| `joints0` values < joint count | `uint8` and 5 joints → `maxJoints` 256 |
| `weights0` sums to 1 within 1e-4 | per vertex, checkable only once `character.bin` exists |
| exactly one `default="true"` per set | all four sets |
| `colorspace` agrees with the variant's `vkFormat` | srgb slots → `*_SRGB*`, linear slots → `*_UNORM*` |
| no orphan `<asset>` | 8 assets, each named by some `<image assetId>` |
| `layer` requires `kind="2dArray"` | one `layer="1"`, on the emissive slot, whose two assets are arrays |
| `layer < layerCount` | checkable only once the two emissive `.ktx2` exist; `layerCount` must be ≥ 2 |
| index range | values in `[0, 256)` for body, `[0, 128)` for face |
| index count divisible by 3, at least 3 | 600 and 900 |

## What `g3d-tool validate` checks, and what it cannot

A clean run on this file proves the rules that are visible in the document alone:

- root, version, section names and order, id shape and per-category uniqueness, and
  **every** reference of the §2 table — `buffer`, `bufferView`, `skeleton`, `skin`,
  `joint`, `camera`, `light`, `sampler`, morph `target` included;
- the pool arithmetic of [BINARY.md](../../docs/BINARY.md) §3–§7: every stream span
  inside its view, every view inside its buffer, all sums overflow-checked, view starts
  on a multiple of 4 (16 for `elementArrayBuffer`) measured from the pool start,
  `byteStride` a multiple of 4, a stream either text or view and never both;
- the §10 stream table: `componentType` per stream, normalisation, `count` equality with
  `positions.count`, dense morph arithmetic, and quantization `min`/`max` agreement;
- the §9 skeleton rules: `@nodes` ⇔ `<joint>` in both directions, single root,
  cycle-free parents, `inverseBindMatrices.count == joints`, `maxInfluences`,
  `skin` ⇔ skin streams;
- §12–§16 value domains and ranges — `alphaMode`, `colorspace`, `kind`, `topology`,
  `@target`, `@workflow`, camera and light field sets, cone ordering — plus
  `colorspace` cross-checked against each variant's `vkFormat`, `layer` against
  `kind="2dArray"`, `when` parsed against the §8.4 grammar, exactly one `default` per
  set, quaternion norms and non-zero scales;
- every §17 ceiling.

It does **not** check, because these need bytes that are not in this repository:

- that `character.bin` and the eight `.ktx2` files exist, that the pool is exactly
  21 248 bytes, and that the padding bytes are zero-filled;
- index values inside `[0, positions.count)`, per-vertex `weights0` sums, unit normals,
  `tangent.w ∈ {−1, +1}`, and the decoded range of the quantized positions;
- the KTX2 header rules of §8.3, including `supercompressionScheme = NONE`, native
  `vkFormat`, DFD consistency, `kind` vs header topology and `layer < layerCount`;
- the rule-6 agreement of dimensions, layers, faces and levels across a `textureSet`;
- §5 path containment and §3 extension prefixes, which the parser does not enforce yet.

So a passing run here is *not* conformance: it says the document is self-consistent,
not that its bytes are. [docs/FORMAT.md](../../docs/FORMAT.md) §19 keeps the same two
lists as the normative statement of coverage, including the open item on the
`channel*=` value grammar — §12 shows letters in one slot and semantic names in
another, so no validator can check that attribute until the grammar is fixed.

## Reproducing the checks

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/g3d-tool validate samples/v1/character.g3d
ctest --test-dir build --output-on-failure
```
