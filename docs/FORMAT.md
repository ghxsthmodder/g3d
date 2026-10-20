# G3D format specification

**Version:** 1.0 · **Status:** draft · **Extension:** `.g3d` · **Encoding:** UTF-8 XML 1.0
**Binary layout:** [BINARY.md](BINARY.md) (pool `.bin`, package `.g3dp`)

## 1. Scope and conformance

G3D describes a 3D model as readable XML plus, for published content, a binary
buffer pool and KTX2 texture images. v1.0 targets real-time use on Android: GPU-ready
compressed textures, a pool a renderer can `mmap` and index without decoding, and a
document that stays reviewable in a text editor.

**RFC 2119 words.** *must*, *must not*, *shall*, *should*, *may*, *optional* carry
their normative meaning. A **must**-violation is an error: a conforming reader
rejects the document and says which rule broke. A **should**-violation is a warning a
conforming reader reports and may continue past.

Two roles are defined, because "valid" means different things to each:

| role | must do |
|---|---|
| **conforming reader** | reject every error it can detect with bounded resources; never allocate from an unchecked count; never fetch a URL; never execute content |
| **conforming writer** | produce documents that satisfy every rule, including the warnings; publish only via the pool and `.g3dp` (§10, BINARY.md §9) |

The reference parser in this repository implements neither role in full; §19 states
precisely what it does. A document that parses cleanly there is not a conforming
document.

## 2. The main document

Exactly one root, `<g3d version="1.0">`. The version is an attribute on the root, not
a processing instruction. The encoding declaration `<?xml version="1.0" encoding="UTF-8"?>`
is ordinary XML and is optional; no other processing instruction is allowed.

Top-level sections occur **at most once** and **in this order**:

```
metadata, assets, skeletons, mesh, materials, cameras, lights, animations, collisions, scene
```

`scene` is required. Every other section is optional; a document with no `mesh` is a
scene of empty nodes, which is legal and useless. A section appearing out of order, or
twice, or an unlisted element at top level, is an error.

This order is not arbitrary: it is dependency order. `assets` needs nothing;
`skeletons` names nodes that `mesh` will consume via joints and bind matrices; `mesh`
references materials, so `materials` follows it and cross-references backwards;
`cameras`/`lights`/`animations`/`collisions` all reference `scene` node ids, and
`scene` is last so that "declared before used" holds for every forward reference a
single-pass parser needs.

An empty document is still a document:

```xml
<g3d version="1.0"><scene><node id="root"/></scene></g3d>
```

Whitespace, comments and processing instructions do not affect semantics. DOCTYPE,
external DTDs and external entities are prohibited: a model **must not** be able to
make the reader open a network connection or read a file outside the package.
`CDATA` sections are allowed only where a numeric array could never appear, so they
have no use in v1.0 and a writer **must not** emit them.

## 3. Extensions and namespaces

Without this rule the format freezes at v1.0 forever, because "unknown element or
attribute is an error" makes every addition breaking. Namespaces split the risk:
typos inside core stay fatal, while third-party additions become visible and ignorable.

```xml
<g3d version="1.0"
     xmlns:g3dx="https://example.org/g3d/ext"
     extensionsUsed="g3dx_gpu_bench g3dx_pbr_clearcoat"
     extensionsRequired="g3dx_pbr_clearcoat">
```

| situation | behaviour |
|---|---|
| unknown element/attribute **without** a namespace prefix | **error** — protects core grammar from typos |
| prefixed name whose prefix is declared in `extensionsUsed` | preserved; a reader that does not implement it **may** ignore it |
| prefixed name whose prefix is in `extensionsRequired` | a reader that does not implement it **must** fail |
| prefixed name in either list, prefix **not** declared with an `xmlns:` | **error** |
| prefix in `extensionsUsed` but not `extensionsRequired` | *safe to ignore*: the core meaning renders correctly without it |

`extensionsUsed` and `extensionsRequired` are space-separated name lists, and every
entry in `extensionsRequired` **must** also appear in `extensionsUsed`. A reader
**must** preserve unknown extension content when it re-emits a document, so that a
round-trip through an intermediate tool is lossless.

An extension **must not** redefine the meaning of a core element or attribute, and
**must not** change how core bytes are laid out — an extension needing a new pool
layout is a format revision, not an extension.

## 4. Identifiers and references

Ids are case-sensitive, unique within their category, and **must** match
`[A-Za-z_][A-Za-z0-9_.-]*`. Categories are fixed:

| category | declared by | referenced from |
|---|---|---|
| asset | `<asset id>`, `<textureSet id>` | `<texture assetId>`, `<image assetId>` |
| skeleton | `<skeleton id>` | `<skin skeleton>` |
| skin | `<skin id>` | `<primitive skin>` |
| joint | `<joint id>` | `skeleton/@nodes`, `<joint parent>` |
| geometry | `<geometry id>` | `<primitive geometry>` |
| buffer | `<buffer id>` | `<bufferView buffer>` |
| bufferView | `<bufferView id>` | stream `view`, `<inverseBindMatrices view>`, `<targetPositions view>` |
| primitive | `<primitive id>` | `<node><mesh ref>`, `channel/@target` |
| material | `<material id>` | `<primitive material>` |
| camera | `<camera id>` | `<node><camera ref>` |
| light | `<light id>` | `<node><light ref>` |
| animation / sampler | `<animation id>`, `<sampler id>` | `<channel sampler>` |
| node | `<node id>` | `channel/@target`, `body/@node`, `<bind node>`, `skeleton/@nodes` |
| collision body | `<body id>` | — |
| morph target | `<target id>` | `primitive/@morphWeights` order only |

Different categories **may** reuse the same string; `<material id="body">` and
`<geometry id="body">` are two different things. `name=` is descriptive and is never a
reference. Every reference must resolve, and a duplicate id within a category is an
error.

`<textureSet>` shares the asset category with `<asset>` deliberately: a material slot
addresses the set, and a set member addresses the image. Two objects in the same
namespace with the same id would make `<texture assetId="x">` ambiguous.

Ids **must not** be the sole ordering mechanism for anything: morph weights and
`skeleton/@nodes` bind by *position*, so reordering those attributes is a semantic
change even when every id still resolves.

## 5. Auxiliary files and paths

A section is inline or external via `src`, never both. An external section's root
element is the section itself (`<mesh>…</mesh>` in `model.mesh.g3d`) and it inherits
the main document's version. Recursion is unsupported: an auxiliary **must not**
itself use `src`.

The path policy is one rule stated once, with "the package" finally defined:

- relative to the main `.g3d`'s directory, or — inside a `.g3dp` — relative to the
  envelope root;
- no absolute paths, no URLs, no drive specifiers, no backslash separators;
- no `..` component, and no resolution that escapes the package root after
  normalisation;
- asset `src`, `pool src` and `<license src>` use the same policy.

`asset/@relativePath="true"` (default) means the path is package-relative; `false`
means an application-private absolute location the format makes no promise about, and
such an asset **must not** appear in a published `.g3dp`.

Within a `.g3dp`, entry names are exactly the strings the document uses, so an
extracted tree and its package address identical files.

## 6. Coordinates, transforms and units

- Right-handed: **+X right, +Y up, +Z forward**.
- Metres for distance, radians for angles, seconds for time. Positive rotation is
  counter-clockwise about the axis when looking from the positive end toward the origin.
- Numeric arrays are whitespace-separated, dot decimal separator, scientific notation
  allowed. Commas, `NaN`, `Inf`, `+` prefixes and locale decimal separators are invalid.
- Every numeric value **must** be finite.
- Column-vector convention: `M_local = T * R * S`, `M_global = M_parent * M_local`. A
  root node's global transform equals its local transform.
- Quaternions are **XYZW**, normalised to within `1e-4`.
- An omitted transform component means identity. Scale components must be non-zero;
  negative scale is allowed (mirroring) and flips winding, which a renderer must
  handle as `doubleSided` would.
- Arbitrary matrices and shear are unsupported. `<transform>` children occur in order
  `translation`, `rotation`, `scale`, each at most once — inside a material texture slot
  `<transform>` means something else entirely (§12) and has no children.

**Image origin is top-left with +V pointing down**, matching Vulkan, glTF and KTX2.
This is fixed by the format version, not declared per document: there is no
`uvOrigin` attribute, and a v1.0 document whose UVs use bottom-up origin is simply
wrong.

That rule is orthogonal to the normal-map convention, which keeps **positive green =
positive tangent-space Y** (the OpenGL-style tangent basis). The two are routinely
confused because most tools that flip one also flip the other, and a normal map whose
green channel is inverted relative to the declared basis lights the model as if the
light were behind it. Nothing in §6 couples them; nothing in §12 changes the origin.

## 7. Metadata

`<metadata>` is descriptive and non-semantic: no other rule in this document reads it.
It exists because a published package that cannot say who made it and under what terms
is a licensing problem waiting to happen.

```xml
<metadata>
  <title>Canonical Character</title>
  <author>G3D Reference Samples</author>
  <createdAt>2026-10-20T14:30:00Z</createdAt>
  <generator name="g3d-exporter" version="1.0"/>
  <license src="LICENSE"/>
</metadata>
```

| element | rule |
|---|---|
| `<title>`, `<author>`, `<copyright>` | free text, at most one each |
| `<createdAt>` | ISO-8601, UTC or explicit offset. `19-10-2026-17-41` is not a date |
| `<generator name version>` | the tool that wrote the file |
| `<license src>` | path under the §5 policy, **or** inline SPDX-style text; `src` and text content are mutually exclusive |
| `<description>` | free text |

`<createdAt>` is the only timestamp in a published package. It is *content*, written
by the exporter, and a deterministic packer **must not** let it drift with build time
unless the model actually changed (BINARY.md §9.5).

## 8. Assets: KTX2, texture sets and variants

### 8.1 `<asset>`

```xml
<asset id="albedo_astc" type="texture" src="textures/albedo_astc.ktx2"/>
```

`id` and non-empty `src` are required; `type` is required and its only v1.0 value is
`texture`. A published asset's `src` **must** end in `.ktx2`. PNG and JPEG are legal
only in an unpacked authoring document, and a packer **must** convert rather than copy
them — the alternative is a format whose published form has two loading paths, one of
which the GPU cannot sample.

That makes `type` redundant in v1.0, deliberately. `<assets>` is the set of
externally-loaded files addressed by ID, and a texture is the only such file the core
grammar has: geometry lives in `<mesh>` and the pool, materials in `<materials>`, metadata
in `<metadata>`. The attribute exists so that a later revision can add a second category —
a compiled shader, an audio clip — inside `<assets>` instead of inventing a parallel
element for it. The shape such an addition **must** take is a reference, not content: an
asset whose `src` names a file the reader hands to the engine. A `.g3d` carrying shader
*source* turns every loader into a compiler and every package into a code-delivery
channel, which contradicts what rules 2 and 3 of §8.3 exist to prevent and defeats the
security imperatives of §18. Terrain splatmapping is the case most likely to need this,
and §12 states what the format can already express without it.

### 8.2 Single source of truth

The KTX2 header is authoritative for `pixelWidth`, `pixelHeight`, `pixelDepth`,
`layerCount`, `faceCount`, `levelCount`, `vkFormat` and sRGB-ness. The XML **must not**
duplicate any of them: `width=`, `height=`, `format=`, `srgb=` on an `<asset>` are
errors, not overrides. A reader **must not** prefer an XML value that contradicts the
container.

What the container genuinely cannot know stays in XML — the slot's role, the device
profile it is intended for, and the image *kind*, declared on the `<asset>`:

```xml
<asset id="emissive_array_astc" type="texture" src="textures/emissive_array_astc.ktx2" kind="2dArray"/>
```

`kind` ∈ {`2d` (default), `cube`, `2dArray`} and is the only statement the XML makes
about image topology. It is an assertion checked against the container, never an
override: rule 5 below rejects a `kind` the header does not support. Omitting it means
`2d`, which is the common case and keeps an authoring file one attribute shorter. The
*number* of layers is not declared here for the same reason the width is not —
`layerCount` is in the header, and two sources for one number drift.

### 8.3 Payload rules

For every `.ktx2` a conforming validator must check, in order:

1. the KTX2 file identifier and header length are present and correct;
2. `supercompressionScheme` **= NONE**. Any other value is an error;
3. `vkFormat` names a **native GPU format** — an UNORM/SNORM/SRGB/SFLOAT/UINT/SINT
   unpacked format, or an ASTC / ETC2 / BC block format. Any Basis Universal codec
   line in the DFD is an error;
4. a DFD is present and consistent with the data offsets, and all `levelCount` levels
   are physically present;
5. `faceCount` / `layerCount` / `pixelDepth` agree with the declared `kind`;
6. all members of one `<textureSet>` share dimensions, layers, faces and levels and
   differ **only** in `vkFormat`. Disagreement is an error, never a silent override;
7. a `<texture layer>` requires `kind="2dArray"` and `layer < layerCount`, taken from the
   header. A `layer` on a `2d` or `cube` image is an error. Ignoring it silently renders
   the wrong slice, which reads to whoever sees it as an art bug rather than a format bug.
   Rule 6 already forces every variant's header to be read and their `layerCount`s
   compared, so this is a bound check against that agreed value, not extra I/O.

Rules 2 and 3 exist because of the target. Basis Universal exists to be transcoded at
load; ETC1S and Zstd supercompression exist to be inflated at load. Both turn "upload
the texture" into "decode the texture first", which costs memory, start-up time and a
transcoder dependency in a format whose whole claim is that the payload is already in
the GPU's language. The cost is honest and stated in §17: a package that ships ASTC,
ETC2 and RGBA8 variants of every texture is larger than one Basis file. On a device
that reports ASTC support — which is the Android target — larger-but-direct beats
smaller-but-deferred.

**Verified constants.** From the KTX 2.0 specification:

| item | value |
|---|---|
| file identifier | `AB 4B 54 58 20 32 30 BB 0D 0A 1A 0A` (12 bytes) |
| header layout | `Byte[12] identifier`, then `UInt32 vkFormat`, `typeSize`, `pixelWidth`, `pixelHeight`, … |
| `supercompressionScheme` | 0 = none, 1 = BasisLZ, 2 = Zstd |

Rule 2 therefore means `supercompressionScheme == 0`, literally, and 1 and 2 are the
two values a validator will meet in the wild: BasisLZ is what a `.ktx2` produced by
`toktx --bcmp` carries, and Zstd is what GDAL's UASTC writer defaults to. Both are
rejected here, so "the file loaded in another engine" is not evidence it is valid G3D.

`vkFormat` is compared as a **name, not a number**. `<image vkFormat>` carries the
`VK_FORMAT_*` token, and a reader resolves that token against the Vulkan registry it
already depends on (`vulkan_core.h`, or vk.xml for a validator) rather than against a
table copied into this document. Embedding the enum numbers would buy nothing — the
reader needs the Vulkan headers anyway to interpret the value — while guaranteeing that
this spec silently disagrees with the registry every time a format is added.

Still **not** fixed here: the `typeSize` rule for block-compressed formats, which the
KTX2 specification states and this draft has not re-verified. A validator takes it from
the KTX2 spec directly. Publishing an integer that has not been checked against its
source is the sort of error that enters a spec and never leaves, which is why the gap is
named instead of guessed.

### 8.4 Variants per device profile

```xml
<textureSet id="albedo_set">
  <image assetId="albedo_astc" vkFormat="VK_FORMAT_ASTC_4x4_SRGB_BLOCK"  when="formatSupported(ASTC_4x4_SRGB)" prefer="30"/>
  <image assetId="albedo_etc2" vkFormat="VK_FORMAT_ETC2_RGB8_SRGB_BLOCK" when="formatSupported(ETC2_RGB8_SRGB)" prefer="20"/>
  <image assetId="albedo_rgba" vkFormat="VK_FORMAT_R8G8B8A8_SRGB"        when="*" default="true"/>
</textureSet>
```

Selection keys on **capabilities queried at runtime** — Vulkan
`vkGetPhysicalDeviceFormatProperties` with `SAMPLED_IMAGE | OPTIMAL_TILING`, or GLES
`GL_COMPRESSED_TEXTURE_FORMATS` — and not on an API level, a SoC whitelist, or a
per-profile package. Rejected for cause:

| rejected | why |
|---|---|
| `apiLevel >= 26` gating | the CDD does not pin a codec to a stable API level; the same level ships devices with different support |
| SoC/manufacturer whitelist | unbounded, and it ages out one GPU generation before the model does |
| one `.g3dp` per profile | multiplies published packages, and a model update becomes N uploads |

`when` is a **closed DSL**, not an expression language:

```
cond := 'formatSupported' '(' NAME ')'
      | 'apiLevel' '>=' INT
      | 'hasExtension' '(' STRING ')'
      | '*'
      | cond (' ' 'and' | ' ') cond
      | 'not' cond
      | '(' cond ')'
```

Anything outside that grammar is an error at validation time, which is the point of a
closed grammar: an offline validator can decide whether a set is satisfiable without a
device. `prefer` is an integer; higher wins among satisfied variants. Exactly one
`<image>` per set must carry `default="true"`. If nothing satisfies and no default
exists, the reader **must** raise an explicit runtime error — never pick a codec the
device cannot sample, because that failure surfaces as a black texture far from its
cause.

A build may still emit per-profile packages as a *strategy*; the format neither
requires nor forbids it.

## 9. Skeletons and skinning

```xml
<skeletons>
  <skeleton id="rig" nodes="hips spine head arm_l arm_r">
    <joint id="hips"  parent=""/>
    <joint id="spine" parent="hips"/>
    <joint id="head"  parent="spine"/>
    <joint id="arm_l" parent="spine"/>
    <joint id="arm_r" parent="spine"/>
    <inverseBindMatrices count="5" view="v_ibm" componentType="float32"/>
  </skeleton>
  <skin id="body_skin" skeleton="rig">
    <bind node="hips" mode="linear"/>
  </skin>
</skeletons>
```

- A `<joint>` **must** reference a `<node>` by the same id, and that node **must**
  exist in `scene`. Joints are nodes: there is no second transform hierarchy to keep
  in sync, which is the only reason `skeleton/@nodes` and `<joint parent>` can be
  validated offline.
- `skeleton/@nodes` is a space-separated joint id list, **and its order is
  normative**: `joints0` indices and `<inverseBindMatrices>` entries both address it
  positionally. Reordering it re-skins the model.
- The joint graph is single-root (`parent=""` occurs exactly once) and acyclic. A
  cycle is an error, not a hang: it is detectable before any traversal.
- `<inverseBindMatrices>` holds one 4×4 column-major matrix per joint, in
  `@nodes` order. `count` **must** equal the number of joints.
- `<skin>` binds a skeleton to the node whose global transform roots it; `mode` is
  `linear` (only v1.0 value) or `dualQuaternion` as an extension candidate.
- `<primitive skin="body_skin">` is what makes a geometry skinned. A primitive
  **must not** carry `joints0`/`weights0` without a `skin`, and **must not** carry a
  `skin` without both streams.

Streams: `joints0` (`uint8` or `uint16`, never normalised) and `weights0` (`uint16`,
normalised; up to 4 influences). `maxInfluences` is 1, 2 or 4 in v1.0 — 8 is an
extension. The joint index limit derives from the component type: `uint8` caps the
skeleton at 256 joints, `uint16` at 65536, and 65536 is a hard cap regardless.

Per vertex, `weights0` **must** sum to 1.0 within `1e-4` (the same tolerance as
quaternions), and any index into `skeleton/@nodes` **must** be in range. Weights for
influences past the last non-zero one **must** be 0, and their joints **must** be the
same index repeated, so a decoder can stop at the first zero.

## 10. Geometry, streams and the pool

A `<mesh>` section contains reusable `<geometry>` elements, `<primitive>` bindings, and
the `<pool>`/`<bufferView>` declarations that locate their bytes.

Each geometry has an id, exactly one `positions` stream and one `indices` stream, and
each optional stream at most once: `normals`, `tangents`, `uv0`, `uv1`, `colors0`,
`joints0`, `weights0`.

| stream | components | allowed `componentType` | normalised |
|---|---|---|---|
| `positions` | 3 | `float32`, `uint16` | `uint16` only with `<quantization>` (signed integers are not valid here — BINARY.md §6) |
| `normals` | 3 | `float32`, `int8`, `int16` | yes for integers |
| `tangents` | 4 | `float32`, `int8`, `int16` | yes for integers |
| `uv0`, `uv1` | 2 | `float32`, `uint16` | `uint16` yes, which limits them to [0, 1] |
| `colors0` | 3 or 4 | `float32`, `uint8`, `uint16` | yes for integers |
| `joints0` | 4 | `uint8`, `uint16` | no |
| `weights0` | 4 | `uint16` | yes |
| `indices` | 1 | `uint16`, `uint32` | no |

Rules:

- Every vertex stream has the same `count` as `positions`. `count` counts **entries**,
  not scalar components.
- `positions.count` ≥ 3; `indices.count` ≥ 3 and divisible by 3.
- Index values lie in `[0, positions.count)`.
- Triangles are the only v1.0 topology; winding is counter-clockwise seen from the
  front. Duplicate vertices at UV seams and hard normal edges are expected, not waste —
  a format that stitched them implicitly could not be validated offline.
- Normals **must** be unit length within `1e-4`. Tangent `w` **must** be exactly −1 or
  +1; stored in `int8` that means ±127, because symmetric normalisation maps 127 to 1.0
  and 128 to −1.0 and would otherwise fail its own rule.
- Colours are RGBA in 0..1. UVs may be outside 0..1 in `float32` mode; a `uint16`
  normalised UV can only express [0, 1], which is why the sample's `face` geometry uses
  `float32` UVs where wrapped coordinates are wanted.

A published geometry **must** use the pool. ASCII arrays are the authoring mode and stay
valid for any document that is not published as a `.g3dp`; the primitives in
`samples/primitives/` are ASCII precisely to keep that mode honest. Inline base64 is
prohibited. Byte-level addressing, component types, alignment, normalisation,
quantization and overflow rules are in [BINARY.md](BINARY.md) §3–§7.

`<primitive>` binds one geometry to one material, may add `skin` and `morphWeights`,
and may reuse a geometry any number of times. Every primitive references an existing
material; there is no implicit default.

## 11. Morph targets

```xml
<morph count="3">
  <target id="blink"/><target id="smile"/><target id="brow"/>
  <targetPositions view="v_morph_pos" componentType="float32" count="384"/>
  <targetNormals   view="v_morph_nrm" componentType="int8" normalized="true" count="384"/>
</morph>
```

- `morph/@count` ≤ 8 targets, and the `<target>` elements are the targets, **in order**;
  `primitive/@morphWeights` is a space-separated weight per position.
- Morph data is **delta** relative to the base stream: `p = base + Σ wᵢ · Δᵢ`.
- Storage is dense: `targetPositions.count` **must** equal `morph/@count ×
  positions.count`, with `targetNormals` matching `targetPositions` when present.
  Sparse deltas are an authoring convenience only, and a packer **must** densify.
- Weights are clamped to [0, 1] for rendering; the initial weights on a primitive must
  already satisfy that. Negative weights (extrapolation) are an extension, not v1.0.
- `morphWeights` is animatable (§14) via `channel/@target` pointing at a **primitive**.

Cubic-spline interpolation and ticks-per-second timing remain unsupported. The v1.0
revision of the old rule is narrow and deliberate: spline *interpolation* needs a
tension/b continuity model nobody has specified, whereas morph *weights* are just a
float array animated by the existing linear sampler. They were conflated in earlier
drafts, which is why "morph unsupported" once covered both.

## 12. Materials and texture slots

```xml
<material id="skin_mat" name="Skin" alphaMode="opaque" doubleSided="false" premultiplied="false">
  <pbr workflow="metallicRoughness">
    <baseColor r="1" g="1" b="1" a="1" colorspace="srgb">
      <texture assetId="albedo_set" texCoord="0"/>
      <transform scale="1 1" offset="0 0" rotation="0"/>
    </baseColor>
    <metallicRoughness>
      <texture assetId="orm_set" texCoord="1" channelG="roughness" channelB="metallic"/>
    </metallicRoughness>
    <metallic value="0"/><roughness value="0.6"/>
    <occlusion strength="0.8"><texture assetId="orm_set" texCoord="1" channel="r"/></occlusion>
    <normal scale="1"><texture assetId="normal_set" texCoord="0" colorspace="linear"/></normal>
    <emissive r="0" g="0" b="0" strength="1"><texture assetId="emissive_set" texCoord="0" layer="1" colorspace="srgb"/></emissive>
  </pbr>
</material>
```

`workflow` is `metallicRoughness` in v1.0; `specularGlossiness` is an extension
candidate. Scalars: base colour RGBA 0..1, metallic and roughness each 0..1,
`emissive.strength` ≥ 0, `occlusion.strength` 0..1, `normal.scale` any finite value
(negative flips the tangent handedness, which is a legitimate mirror fix).

`<material>` attributes: `alphaMode` = `opaque | mask | blend` (default `opaque`);
`alphaCutoff` in 0..1, **required** when `alphaMode="mask"`; `premultiplied`;
`doubleSided`. `blend` ordering relative to other transparent primitives is a render
concern and is not specified — the format's promise is that `alpha` means what the
values say, not that it composites in a defined order.

`<texture>` attributes: `assetId` → asset or textureSet, `texCoord` → UV set index,
`layer` → slice of a `2dArray` image, `colorspace` = `srgb | linear`, and channel
selection `channel`, `channelG`, `channelB` (`channelA` also). The packing convention is
**ORM**: R = occlusion, G = roughness, B = metallic, A = unused — identical to glTF, so
an existing toolchain interoperates without a converter, and `channel*=` exists for
engines that want a different layout.

**Atlas arrays.** `layer` samples one slice of a `kind="2dArray"` image, so a family of
related surfaces becomes one allocation, one sampler binding, one mip chain, and a
choice of appearance reduced to a non-negative integer. In `samples/v1/character.g3d` the
emissive detail is drawn from an array shared across a character set:

```xml
<assets>
  <textureSet id="emissive_set">
    <image assetId="emissive_array_astc" vkFormat="VK_FORMAT_ASTC_4x4_SRGB_BLOCK" when="formatSupported(ASTC_4x4_SRGB)" prefer="30"/>
    <image assetId="emissive_array_rgba" vkFormat="VK_FORMAT_R8G8B8A8_SRGB"       when="*" default="true"/>
  </textureSet>
  <asset id="emissive_array_astc" type="texture" src="textures/emissive_array_astc.ktx2" kind="2dArray"/>
  <asset id="emissive_array_rgba" type="texture" src="textures/emissive_array_rgba.ktx2" kind="2dArray"/>
</assets>
<!-- in the same document's <material id="skin_mat">, the slot that reads it: -->
<emissive r="0" g="0" b="0" strength="1">
  <texture assetId="emissive_set" texCoord="0" layer="1" colorspace="srgb"/>
</emissive>
```

No shader is involved, and that is the point: the standard pipeline already samples
`vec3(uv, float(layer))`, so the format states the index and nothing else. Which layer a
given model uses is a **content** decision and stays in the game manifest — the
application's own file, which names a `@id` or `@name` and rebinds it. G3D's obligation
is to make the layer addressable, its bounds checkable, and `<material>@id`/`@name`
stable enough for that rebinding to survive a re-export. This is also why `@when` in §8.4
speaks only of device capability and never of content: capability selection and content
selection are different axes, and giving them one syntax would make a per-device package
look like a per-art-variant package.

Terrain splatmapping is the honest hard case. A control texture plus N detail layers is
not an index but a *blend*, and the format has no words for the combining function. What
it can express today is the data half — control texture and detail arrays as assets,
`layer` naming each slice, weights as an ordinary channel-selected texture. The remaining
half is exactly the scenario §8.1 reserves a future `type` value for: a shader
**reference** the reader resolves against the engine's own program store, declared in
`extensionsRequired` (§3) so a reader that cannot honour it fails loudly instead of
substituting a flat default look. That reference is not defined in v1.0; nothing here
prevents defining it later without touching the grammar that exists now.

`<transform scale offset rotation>` on a slot is a UV transform. It is **not** the
`<transform>` of §6: same element name, different meaning, different attributes,
disambiguated by parent. That collision is accepted knowingly, because a v1.0 reader
can always tell which it is from the parent chain — but a future revision that adds a
third meaning must rename rather than overload.

`colorspace` is *intent* and is cross-checked against the resolved variant's
`vkFormat` (§8.3 rule 6): an `srgb` slot must resolve to a `*_SRGB_*` format and a
`linear` slot to `*_UNORM_*`/`*_SFLOAT_*`. A mismatch is an error in both directions —
a double-corrected albedo is darker than intended and a hand-edited normal map is
spiky, and neither looks like a format error to whoever sees it.

## 13. Cameras and lights

```xml
<camera id="cam_main" type="perspective">
  <yfov value="0.7854"/><aspectRatio value="1.7778"/><znear value="0.1"/><zfar value="12"/>
</camera>
<light id="key" type="spot">
  <color r="1" g="0.95" b="0.9"/><intensity value="1200"/><range value="8"/>
  <innerConeAngle value="0.3"/><outerConeAngle value="0.5"/>
</light>
```

`camera/@type` = `perspective` (needs `yfov` in radians, `znear` > 0, `zfar` > `znear`;
`aspectRatio` optional — absence means derive from viewport) or `orthographic` (needs
`xmag`/`ymag` > 0). `light/@type` = `directional` (colour + intensity, no range),
`point` (intensity + range) or `spot` (adds `innerConeAngle` ≤ `outerConeAngle` ≤ π/2).
Ranges are metres; `range` omitted means unbounded.

The shapes mirror `KHR_lights_punctual` for interop, but attachment uses the document's
own idiom — `<node><camera ref/><light ref/></node>` — rather than a KHR extension
mechanism, because nodes already carry transforms. A node may carry at most one
`<camera>` and any number of `<light>` elements. Cameras and lights are inert until
attached: a declared-but-unattached camera is legal and meaningless, which is why
`scene`-attachment is the normative way to say "this camera exists in the world".

Photometric units (candela, lux) are a **documented convention, not a normative
requirement**: v1.0 validates ranges and signs, not physical dimensions. Imposing
candela before there is a renderer that consumes them would create a spec that is
right about physics and wrong about what implementations do.

## 14. Animations

```xml
<animation id="idle" duration="2.5">
  <sampler id="spine_rot">
    <input  count="3">0 1.25 2.5</input>
    <output count="3">0 0 0 1  0 0.049979 0 0.99875  0 0 0 1</output>
  </sampler>
  <channel target="spine"     property="rotation"     sampler="spine_rot" interpolation="linear"/>
  <channel target="face-prim" property="morphWeights" sampler="face_morph" interpolation="linear"/>
</animation>
```

- `duration` is finite and > 0 seconds. Times are seconds from animation start.
- `<input>` is strictly increasing, non-negative, ≤ duration. `<output>` holds one
  element per input, `count` matched.
- `property` = `translation` (3), `rotation` (4, XYZW, SLERP under `linear`), `scale`
  (3) or `morphWeights` (`morph/@count` values).
- `channel/@target` resolves against **nodes and primitives** — the same attribute
  addressing two categories is why §4 lists both, and why the reference in §2 has to be
  a category-aware lookup rather than a global id set.
- `interpolation` = `step` | `linear`. Cubic spline is unsupported.
- A (target, property) pair must not repeat within one animation.
- Samplers may use ASCII arrays or a `view` reference (§10 arithmetic applies).
- Extrapolation outside [0, duration] is `clamp` — the only mode that cannot surprise a
  viewer scrubbing a timeline.

A target/property pair animating `morphWeights` and a static `primitive/@morphWeights`
disagreeing is not an error: the animation wins at runtime, and the static value is the
authoring default.

## 15. Collision data

```xml
<body id="hips_col" node="hips" type="dynamic">
  <shape type="capsule" radius="0.25" height="1.2"/>
</body>
```

`body/@node` → node id; `type` = `static`, `dynamic`, `kinematic`. Shapes: `box` (full
positive `extent x y z`), `sphere` (`radius` > 0), `capsule` (`radius`, `height` > 0,
axis +Y, `height` measuring the cylindrical section), `cylinder` (same), `mesh`
(`geometry` reference, `convex` or `staticTriangleMesh`). A `staticTriangleMesh`
requires a `static` body; dynamic triangle meshes are unsupported — that is the engine
constraint, not a limitation of the format.

Shape-local `translation`/`rotation` are optional; local **scale is unsupported**,
because baking scale into a primitive shape gives a better-conditioned hull than a
transformed one. Mass, friction and restitution are outside the v1.0 contract on
purpose: they are engine-specific, and a format that guesses them gets blamed for the
guess.

Multiple `<shape>` children per `<body>` are allowed and mean a compound collider.

## 16. Scene

`<scene>` contains exactly one root `<node>`. `node/@id` is required and unique; nesting
is by element containment, not by a `parent=` attribute, so a scene cannot contain a
cycle and does not need a graph walk to be validated — depth is bounded instead (§17).

```xml
<scene>
  <node id="root">
    <node id="hips" translation="0 0 0">
      <node id="spine" translation="0 0.5 0"/>
      <node id="body"><mesh ref="body-prim"/></node>
    </node>
    <node id="camnode" translation="0 1.4 2.2"><camera ref="cam_main"/></node>
    <node id="keynode" translation="1.5 2.5 1.5"><light ref="key"/></node>
  </node>
</scene>
```

A node may carry any mix of: child nodes, `<mesh ref>` (a primitive instance),
`<camera ref>` (at most one), `<light ref>` (any number). `translation`, `rotation`,
`scale` follow §6. Nodes whose ids are joint ids are the joints (§9) and must exist here.

## 17. Normative limits

"Should limit" is not testable, so these are hard numbers with reasons. A conforming
reader **must** reject a document that exceeds them, and a validator **must** check
them. A conforming writer **must not** produce one.

| quantity | limit | why |
|---|---|---|
| document size | 8 MiB | parsing a model that big in one string is a memory event on a phone |
| XML nesting depth | 64 | bounds recursion without a heap check |
| `positions.count` | 16 777 215 | 24-bit index ceiling, consistent with `uint32` storage |
| geometries per document | 65 535 | id-space hygiene, and an exporter bug usually looks like this |
| primitives per document | 65 535 | draw-call sanity |
| nodes in scene | 65 535 | a hierarchy beyond this is a data-generation bug |
| joints per skeleton | 256 / 65 535 by `joints0` type, hard cap 65 535 | derived from the index type (§9) |
| `maxInfluences` | 4 (8 by extension) | GLES baseline skinning |
| `morph/@count` | 8 | morph streams are dense; 8 targets × positions is already large |
| textures per document | 256 | variant sets multiply quickly and this bounds them |
| `textureSet` members | 8 | beyond that, profiles are a build-time concern |
| indices per primitive | 16 777 215 | same ceiling as vertices |
| `levelCount` | 16, dimensions power-of-two when uncompressed | mips of a 2¹⁶ texel; mip completeness is §8.3 rule 4 |
| entries per `.g3dp` | 65 536 | BINARY.md §9.7 |
| entry name length | 1 024 bytes | path budget |
| single entry | 4 GiB − 1 | BINARY.md §9.7 |

These numbers are a proposal awaiting the owner's confirmation (§19 open items). They
are stated as *must* rather than *suggested* even while provisional: a validator cannot
test an omitted threshold, and "should limit" invites an implementation that limits
nothing.

## 18. Validation and security

A conforming validator rejects: malformed XML, unsupported versions, unknown
non-namespaced elements/attributes, extension names in `extensionsRequired` it does not
implement, duplicate ids within a category, unresolved references, non-finite or
misformat numbers, wrong array lengths, out-of-range indices, invalid transforms,
invalid body/shape combinations, pool containment and alignment violations, KTX2
violations of §8.3, and every §17 limit.

Schema validation cannot enforce cross-references, hierarchy shape, path containment,
quaternion norms, weight sums, count arithmetic or pool offsets. Those are semantic,
and semantic rules belong in a validator with real arithmetic — which is why this
repository treats the C++ library, not a schema file, as the normative checker
(§19, and the open item on `scheme/`).

Security requirements, stated as imperatives because a parser tends to forget them:

- **Never** allocate from an unvalidated `count`, `byteLength` or `entryCount`. Check
  against the declared ceiling before the allocation, with checked arithmetic.
- **Never** sum two offsets in an integer type whose width the file chose for you.
- **Never** resolve a path containing `..`, an absolute path, a URL or a backslash, and
  never re-resolve after normalisation has escaped the package root.
- **Never** fetch a network resource, execute a script, or load a library named by a
  model. DOCTYPE and external entities are prohibited for exactly this reason.
- **Never** trust a container's self-reported dimensions over its own header (§8.2),
  and never trust an XML attribute over the bytes it describes.
- Bound decompression: KTX2 in v1.0 carries no compressed payload (§8.3 rule 2), so
  "inflate bomb" is not a KTX2 concern — but a `.g3dp` that later grows compression via
  `flags` bit 0 inherits it, and the cap must be designed in then, not added after.

## 19. What the reference parser implements today

`src/model.cpp` checks: root element and version, tag balance and entity syntax,
top-level section names, order and duplication against §2, required `scene`, id presence
and per-category uniqueness for `asset`/`textureSet`/`geometry`/`primitive`/`material`/
`node`, and reference resolution for `primitive/@geometry`, `primitive/@material`,
`node/mesh/@ref`, `channel/@target` (nodes **or** primitives), `body/@node`, and
`assetId` from `<texture>` and `<image>`. It records section `@src` paths without
loading them.

It does **not** yet check — and a clean `g3d-tool validate` run therefore proves none
of this:

- numeric semantics: array lengths vs `count`, index ranges, quaternion and normal
  norms, weight sums, quantization `min`/`max` agreement, §17 limits;
- pool semantics: existence and size of `pool/@src`, offsets, containment, alignment,
  `byteStride` divisibility, `componentType` domain, view resolution;
- KTX2: file existence, header rules of §8.3, variant-set cross-checks, `colorspace`
  vs `vkFormat`, `kind` agreement and `layer` bounds against `layerCount`;
- reference categories it does not model yet: `buffer`, `bufferView`, `skeleton`,
  `skin`, `camera`, `light`, `sampler`, `animation`, morph `target`;
- structure: scene-root cardinality, node depth, joint graph shape and cycle-freedom,
  joints-are-nodes agreement, path containment (§5), extension prefix rules (§3);
- value domains: `alphaMode`, `colorspace`, `componentType`, `topology`, `kind`,
  `when` DSL syntax;
- `.g3dp`: nothing. There is no reader, no writer, and no packer in this repository.

The same list is mirrored in `samples/v1/README.md`, so a reader validating the
canonical sample cannot mistake a structural pass for semantic conformance.

**Open items carried by this draft**, in order of blocking power:

1. The KTX2 identifier, `supercompressionScheme` values and header field order are now
   fixed in §8.3. What remains unverified is the `typeSize` rule for block-compressed
   formats, which a validator reads from the KTX2 specification until it is confirmed.
2. The §17 limit numbers — proposed, awaiting confirmation.
3. `scheme/model.g3d.sch` — empty. Either it becomes a structure-only Relax NG schema
   beside the semantic C++ validator, or it is removed and the repository admits there
   is no declarative schema for v1.0. Both are defensible; an empty file is not.
4. Basis Universal and supercompression (§8.3) stay prohibited until a target device
   population makes transcoding cheaper than variant shipping — that is a measurement,
   not a decision anyone in this repository has made.
5. A shader **reference** (§8.1, §12) as `extensionsRequired` content: needed by terrain
   splatmapping, deferred by everything else. Before it can be drafted, two questions
   have answers the repository does not yet have — whether the reference names one
   program per API (GLES GLSL and Vulkan SPIR-V are not interchangeable on the Android
   target) or one program with per-API bindings, and whether a material with an
   unresolvable reference is an error or a documented fallback to `<pbr>`. The second is
   a product decision about visual failure, not a grammar decision.

## 20. Reference samples

| path | mode | covers |
|---|---|---|
| `samples/v1/character.g3d` | pool + KTX2 + variants | every section of §2–§17; see its README for the hand-computed offset tables |
| `samples/primitives/*.g3d` | pure ASCII, self-contained | §2, §4, §6, §7-free, §10, §12, §16 — proof the authoring mode needs no binary file |

`samples/v1/` references `character.bin` and `textures/*.ktx2`, which are **not** in
this repository. They are absent rather than faked: producing them means writing the
packer and a KTX2 encoder, which is the next deliverable, and a placeholder file would
pass the structural checks while teaching readers that the pool layout is arbitrary.
The byte arithmetic those files must satisfy is fully specified in
`samples/v1/README.md` and [BINARY.md](BINARY.md).

Samples are fixtures, not a guarantee that every rule above is already enforced.
