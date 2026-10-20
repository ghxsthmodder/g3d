# G3D binary layout

**Version:** 1.0 · **Status:** draft · **Extensions:** `.bin`, `.g3dp` · **Byte order:** little-endian

This document defines the two binary artefacts of G3D v1.0: the geometry **pool**
(`<stem>.bin`) referenced by `<pool src>` in [FORMAT.md](FORMAT.md), and the
**package envelope** (`.g3dp`) that carries the document plus its assets as one file.
The XML is the authority on what the bytes mean; this document defines where they are.

## 1. Normative language

The words **must**, **must not**, **shall**, **should**, **may** and **optional** are
used as in RFC 2119. A reader that encounters a **must**-violation **must** reject the
file with an error; it **must not** silently repair, clamp or reinterpret the value.

## 2. Byte order and the absence of a pool header

Every multi-byte component in a pool is **little-endian**, always. There is no
endianness field, because there is nothing it could say: a value that is not
little-endian is not a G3D pool. A reader **must not** attempt to byte-swap a pool
it failed to parse.

The pool file has **no header, no magic, no footer and no index**. Its content is
exactly `pool/@byteLength` bytes and nothing else. This is a deliberate rejection of
a self-describing container:

| | raw pool (chosen) | `G3DB` header + manifest (rejected) |
|---|---|---|
| where a buffer offset lives | XML only | XML **and** the manifest |
| address of a component | `buffer.byteOffset + view.byteOffset + stream.byteOffset + i*stride` | same, after parsing a manifest |
| mmap cost | map the file, use offsets | one extra parse per file open |
| drift risk | none | XML and manifest can disagree |
| identify the file type | by convention/`.g3dp` entry kind | by magic |

A duplicated offset table is the failure mode: two sources of truth, and every
implementation that disagrees picks a different one. The last row is the only real
loss, and it is acceptable — a pool is never handed to a stranger; it is named by a
`<pool src>` in a document the reader already parsed.

Consequence for tooling: a pool **must not** be opened by path inference. The reader
**must** resolve `pool/@src` against the document's directory (or, inside a `.g3dp`,
against the entry table) and never guess.

## 3. Address computation

Three offset spaces exist and they nest. Getting them wrong is the single most common
way to corrupt a pool, so the definitions are normative and exhaustive:

| attribute | measured from |
|---|---|
| `buffer/@byteOffset` | start of the pool file |
| `bufferView/@byteOffset` | start of its `@buffer` |
| stream `@byteOffset` (e.g. `positions/@byteOffset`) | start of its `@view` — i.e. inside `@byteStride` when the view is interleaved |

The absolute file offset of element *i* of a stream:

```
buffer.byteOffset + view.byteOffset + stream.byteOffset + i * (view.byteStride ? view.byteStride : elementSize)
```

`elementSize` is `componentSize × components` regardless of the stride — `byteStride`
changes the step between elements, never their width.
All three offsets **must** be summed in 64-bit arithmetic with overflow checked before the
comparison against a length; `u32` addition of two attacker-supplied offsets is the classic
way a pool validator reads past the end of a mapping.

## 4. Alignment

| rule | value | applies to |
|---|---|---|
| `componentSize` divides the component's own offset | 1, 2, 4 | every stream |
| view start in the pool | multiple of 4 | all views |
| view start in the pool | multiple of 16 | a view whose `@target` is `elementArrayBuffer` |
| `buffer/@byteOffset` | multiple of 256 | a buffer holding texel or storage-image data |
| `buffer/@byteOffset` inside a `.g3dp` | multiple of 4096 | the pool entry (page boundary, for copy-free `mmap`) |
| `byteStride`, when non-zero | multiple of 4 | interleaved views |

"View start in the pool" is `buffer/@byteOffset + bufferView/@byteOffset`, not the
`bufferView/@byteOffset` alone: §3 makes that attribute relative to its buffer, and a
reader pointer-casts at the absolute byte. `bufferView/@target` is optional and absent
means `arrayBuffer`; `buffer/@align` is optional and absent means 1.

`buffer/@align` records the strictest alignment a buffer needs, so a packer can lay
out buffers without knowing which views will land inside them. It **must** be a power
of two and **must** be satisfied by `buffer/@byteOffset`; `buf_static align="256"` in
the canonical sample means exactly that.

Padding between buffers and inside an interleaved stride **must** be zero-filled.
Non-zero padding is a validation error: it makes a byte-for-byte comparison of two
packs of the same model ambiguous, and it leaks whatever bytes the packer had in
memory.

A stride whose attributes do not start on their own component boundary is a **warning**,
not an error, and the reader **must** handle it — some exporters legitimately pack a
`uint8` flag after a `uint16` pair. The alignment rules above are what a *conforming
packer* must produce.

## 5. `componentType`

| `componentType` | bytes | signed | integer range | valid for |
|---|---|---|---|---|
| `float32` | 4 | yes | IEEE-754 binary32 | any stream |
| `int8` | 1 | yes | −128..127 | normals, tangents |
| `uint8` | 1 | no | 0..255 | joints, colors |
| `int16` | 2 | yes | −32768..32767 | normals, tangents |
| `uint16` | 2 | no | 0..65535 | indices, UVs, weights, quantized positions |
| `int32` | 4 | yes | — | **not** valid in v1.0 |
| `uint32` | 4 | no | 0..4294967295 | indices only |

`float64` is not available: it doubles the size of every position stream for
precision no real-time renderer uses. `int32` has no honest use. Any other value is an
error.

`float16` is deliberately absent from v1.0 — support in GLES is extension-dependent,
and a format that needs a capability query to know whether its *numbers* are readable
has lost something. It is a candidate for `g3dx_` extension.

## 6. Normalisation and quantization

`normalized="true"` maps integers to `[0, 1]` (unsigned) or `[-1, 1]` (signed):

```
unsigned  f = q / (2^bits − 1)
signed    f = max(q / (2^(bits−1) − 1), −1)      // symmetric: −32767 → −1, −32768 is invalid
```

Symmetric signed normalisation is the rule for **direction** attributes — normals and
tangents — because it maps `±32767` to exactly `±1`, which is what the tangent sign
convention needs, and because `q = 0` decodes to `0.0` rather than to a small bias.
It is **not** the rule for positions: a quantized position spans a bounding box that
does not straddle zero, so the map is affine:

```
c = q / (2^bits − 1)          // q must be unsigned
p = offset + c * scale
```

`<quantization offset scale min max>` states the box. The validator **must** enforce
`min == offset` and `max == offset + scale` component-wise, with a tolerance of
`1e-6 * max(|min|, |max|, 1)`, and **must** reject a quantized stream whose
`componentType` is signed. Two numbers that can disagree with the data and with each
other is already a compromise; three would be indefensible — but `min`/`max` earn
their place because a renderer wants the bounds without decoding every vertex.

Precision for a `uint16` position stream is `scale / 65535` per axis, roughly 11 to 18
micrometres for the sample's character-sized box, against 12 bytes per vertex as
`float32`.

A stream with `normalized="false"` and an integer `componentType` **must** be decoded
as that integer; joints use this (`uint8` indices, no normalisation).

## 7. ASCII arrays and the two authoring modes

v1.0 has two legal ways to store a stream and they may be mixed inside one document:

```xml
<positions count="3">0 0 0  1 0 0  0 1 0</positions>          <!-- ASCII -->
<positions count="256" view="v_body_verts" byteOffset="0"      <!-- pool -->
                componentType="uint16"/>
```

A stream is either an ASCII text node or a `view=` reference, never both, and never
neither. ASCII mode is for hand-written and exported-but-unpacked documents; a
published package **must** use the pool (see FORMAT.md §10). The primitives in
`samples/primitives/` are ASCII on purpose: they prove a model can be complete with no
binary file at all.

Inline base64 is **prohibited**. It is neither compact (33% larger) nor fast (decode
pass before use), and it makes `count` arithmetic a lie.

One containment rule covers both layouts. With `step = view.byteStride != 0 ? view.byteStride : elementSize`:

```
stream.byteOffset + (count − 1) * step + elementSize  <=  view.byteLength
```

Both the multiplication and the addition **must** be checked for `u64` overflow before
the comparison. In the canonical sample the tightest stream of the interleaved view is
`weights0`: `28 + 255*36 + 8 = 9216`, exactly `v_body_verts.byteLength`.

## 8. File naming

| artefact | name | when |
|---|---|---|
| document | `<stem>.g3d` | authoring |
| pool | `<stem>.bin` | authoring, referenced by `pool/@src` |
| external section | `<stem>.<section>.g3d` | a section split out via `@src` |
| package | `<stem>.g3dp` | published, single file |
| pool inside a package | `pool.bin` | always the canonical name |

Within a `.g3dp` the pool entry is **`pool.bin`**, not `<stem>.bin`, so that a package
built from a differently-named source is byte-identical to one that is not. Entry
names are the same relative paths the document uses in `@src`, with no rewriting, so
an extracted tree and a package address the same files by the same strings.

## 9. `.g3dp` envelope

A `.g3dp` is **a tar of exactly the kind G3D needs**: fixed-layout index, entries
stored uncompressed and aligned, no directory entries, no timestamps, no owner fields.
The bytes on disk *are* the KTX2 image, the pool, the XML.

### 9.1 Why uncompressed

The payload of a `.g3dp` is already compressed or already random: KTX2 blocks are
GPU-addressable bytes, a pool is quantized floats, and XML compresses well but has to
be read whole anyway. Compressing them again buys nothing and costs the property that
makes the envelope worth having: an entry can be `mmap`ed and read in place, with no
extraction and no inflate. A packed entry would also break the containment rule a
reader uses to compute addresses — after inflate, the offsets in the XML refer to a
buffer that no longer exists on disk.

This is also the reason FSX is not the envelope. FSX is a fine *distribution*
container, and G3D may be wrapped in one; but its guarantees are the opposite of the
ones a renderer needs: chunks of 32 KiB–8 MiB with random padding between them
(`pad_min 0..pad_max 4` even in the most benign layout 0), a codec drawn per file, an
index that is zstd-compressed and XOR-masked, and an API that hands the caller the
whole container as `&[u8]`. Random access to one 9 KiB vertex view through that design
means "decompress and reassemble everything". `.g3dp` is thin and boring for exactly
the one job of being read by a GPU driver's loader.

### 9.2 Header — 64 bytes

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | magic | `G3DP` = `47 33 44 50` |
| 4 | 1 | formatVersion | `1` |
| 5 | 1 | headerSize | `64`; a reader **must** skip `headerSize − 64` extra bytes rather than reject a later version |
| 6 | 2 | flags | bit 0 compression — **reserved, must be 0**; bit 1 digest present; bits 2–15 reserved, must be 0 |
| 8 | 4 | entryCount | |
| 12 | 4 | mainXmlEntry | index into the entry table of the document; `0xFFFFFFFF` if none |
| 16 | 4 | nameBlobLength | |
| 20 | 4 | — | reserved, must be 0 |
| 24 | 8 | entryTableOffset | |
| 32 | 8 | entryTableLength | record area plus name blob |
| 40 | 8 | payloadEnd | first byte after all content |
| 48 | 1 | contentDigestKind | 0 none, 1 sha256 |
| 49 | 3 | — | reserved, must be 0 |
| 52 | 8 | contentDigestOffset | 0 when `contentDigestKind == 0` |
| 60 | 4 | headerCRC32 | CRC32 of bytes `[0, 60)` |

`headerCRC32` is the last field precisely so that the CRC covers every field that
tells the reader where to look. A reader **must** verify it before trusting
`entryTableOffset`, and **must** treat a mismatch as "this is not a G3D package",
not as "let me try the table anyway".

### 9.3 Entry record — 48 bytes

| offset | size | field | notes |
|---|---|---|---|
| 0 | 4 | nameOffset | into the name blob |
| 4 | 2 | nameLength | ≤ 1024 |
| 6 | 1 | kind | 0 xml, 1 ktx2, 2 bin, 3 other |
| 7 | 1 | — | reserved, must be 0 |
| 8 | 8 | fileOffset | absolute in the `.g3dp` |
| 16 | 8 | storedLength | bytes on disk |
| 24 | 8 | originalLength | **must** equal `storedLength` (STORED) |
| 32 | 4 | alignment | the alignment the entry was placed at |
| 36 | 4 | crc32 | over the entry's stored bytes |
| 40 | 8 | — | reserved, must be 0 |

Then the name blob: UTF-8 paths, packed with no separators and no terminators, in the
order the records name them. Records **must** be sorted by name (byte-wise, unsigned)
so a reader can binary-search and so two packs of the same tree produce the same file.

### 9.4 Layout and placement

```
64-byte header | entry 0 | pad | entry 1 | pad | … | entry table | name blob | [digest] | pad
```

Each entry starts at the next multiple of its required alignment:

| kind | alignment | why |
|---|---|---|
| `xml` | 16 | cheap, and keeps the document off the pathological 1-byte boundary |
| `ktx2` | 256 | the same 256 the pool uses for texel/storage views, so a loader sees one alignment rule for all image data |
| `bin` | 4096 | page boundary: the pool is `mmap`ed and its internal offsets must survive being mapped |

Padding **must** be zero-filled, for the same determinism reason as pool padding.
The file **must** end at a multiple of 8 so `payloadEnd` can be read with one 64-bit
load.

### 9.5 Determinism

A pack of a given document tree, given the same inputs, **must** be byte-identical on
any machine, any time. That requires all of:

- no timestamps, no file mtimes anywhere in the envelope (the XML `<createdAt>` is
  content, not packaging metadata, and is the only time stamp present);
- entries sorted by name;
- zero-filled padding in the pool, between entries, and after the table;
- `flags` and `contentDigestKind` chosen by the packer deterministically, not derived
  from environment;
- no filesystem order dependence: the packer sorts by the *name string*, never by
  `readdir` order.

A checker or a cache can then key on the file hash instead of re-parsing, and a test
can assert that two packs of the same sample are the same bytes.

### 9.6 Reading a `.g3dp`

Reference algorithm, with the failure mode of each step stated. Every offset
arithmetic here is `u64` and **must** be checked.

1. The file is at least 64 bytes. Read the header. Verify magic, `formatVersion`,
   `headerSize ≥ 64`, reserved fields zero, `flags` bit 0 clear, `headerCRC32`.
2. `entryTableOffset + entryTableLength ≤ payloadEnd ≤ file size`, computed without
   overflow. A `.g3dp` whose table claims to be past its own end is a hostile file,
   not a truncated one; the reader **must not** allocate from `entryTableLength` before
   this check passes.
3. `entryCount * 48 ≤ entryTableLength`. Compute the name blob length as
   `entryTableLength − entryCount*48` and verify it equals `nameBlobLength`.
4. For each record: `nameOffset + nameLength ≤ nameBlobLength`; the name is valid
   UTF-8, is a relative path, contains no `..` component, no backslash, no leading `/`,
   and no drive specifier. Reject before using it — this is the path-traversal gate.
   `fileOffset + storedLength ≤ payloadEnd`. `originalLength == storedLength`.
   `fileOffset % alignment == 0`.
5. Entries **must** not overlap each other or the entry table. Sort by `fileOffset`
   and check pairwise; a package that overlaps is corrupt, and reading it "anyway"
   yields a model whose geometry silently aliases a texture.
6. Verify each entry's `crc32` against its bytes. On mismatch, report the entry name —
   a single corrupt KTX2 should not be diagnosed as "the model failed to load".
7. Hand the document entry to the XML parser. Resolve `pool/@src` and each
   `asset/@src` against the entry table by name, not against the filesystem.
8. Verify the content digest when `contentDigestKind != 0`. This is **recommended**
   integrity, not authentication: a CRC-32 and a SHA-256 both defend against
   corruption, and neither defends against an adversary who can rewrite the digest.
   Verifying that a package came from a trusted publisher is signature policy and
   belongs to the application, not to the format.

### 9.7 Limits

| quantity | limit | rationale |
|---|---|---|
| entries per package | 65 536 | keeps a malformed `entryCount` from asking for 3 MiB of table |
| entry name length | 1 024 bytes | path budget |
| single entry | 4 GiB − 1 | `storedLength` is 8 bytes; the cap is policy, and pools that big want splitting |
| header + table | 8 MiB | derived from the entry cap |
| nesting of `.g3dp` in `.g3dp` | none | FSX wrapping is external and is not visible to a G3D reader |

A reader **should** enforce these before allocation. A validator **must** report a
violation as an error naming the quantity that broke.

## 10. Integrity summary

| layer | mechanism | catches |
|---|---|---|
| pool file | `pool/@byteLength` vs actual size, zero-padding check | truncation, garbage tail |
| `.g3dp` header | `headerCRC32` | any edit of the fields that point at the table |
| `.g3dp` entry | per-entry `crc32` | a corrupted texture or pool |
| `.g3dp` content | optional SHA-256 | accidental modification of the whole package |
| document | XML well-formedness + reference resolution | everything semantic |

None of these is a signature. G3D v1.0 makes no authenticity claim and no tamper
*detection* promise beyond CRC: an attacker who can rewrite the file can rewrite the
CRCs. Anti-tamper is the job of the distribution layer (FSX's AEAD with AAD over the
header exists precisely for this), and pretending otherwise in the format spec would
be the kind of overclaim that gets a format trusted where it should not be.

## 11. What is not defined here

- Mip-generation and level selection policy for KTX2 (the header's `levelCount` is
  authoritative; how a renderer chooses is not a format question).
- Compression of the XML inside the envelope: reserved by `flags` bit 0 and fixed at
  zero for the whole of v1.0. A future version that wants it **must** define how pool
  offsets survive inflate, because that is the hard part, not the flag.
- Any `.g3dp`-level encryption. Distribution DRM is FSX's role.
- Basis Universal and all KTX2 supercompression: prohibited by FORMAT.md §8.3, and no
  flag here reopens that.
