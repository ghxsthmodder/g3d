# G3D — Geometry Three-Dimensional

G3D is a 3D model format based on readable XML and auxiliary files for geometry, materials, textures, animations, and collision data. This repository provides an early C++20 library and command-line validator.

> **Development status:** early implementation. The parser and the byte-free validator target the draft v1.0 root `<g3d version="1.0">`. Between them they check every rule that is visible in the document alone: structure, ids, references, value domains, §17 limits and the pool arithmetic of BINARY.md §3–§7. Neither reads the `.bin` pool nor a `.ktx2` payload, so a clean `g3d-tool validate` is not conformance — [docs/FORMAT.md](docs/FORMAT.md) §19 states the boundary.

## Build

Requirements: CMake 3.20+, a C++20 compiler, and CTest.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## CLI

```sh
g3d-tool validate path/to/model.g3d
g3d-tool inspect path/to/model.g3d
g3d-tool --help
```

`g3d-tool validate` runs the parser and then the validator: root and version, top-level section names and order, id shape and per-category uniqueness, reference resolution for every category of the §2 table, the value domains of §8–§15, the geometry, skeleton, material, camera, light, animation and collision arithmetic, the pool rules of BINARY.md §3–§7 that need no bytes, and every §17 ceiling. XML handling is a deliberate subset — no DTD, external entities, CDATA or processing instructions — and `>` inside a quoted attribute value is handled, because the §8.4 `when` grammar writes `apiLevel >= 26`. Section `@src` paths are recorded, not loaded, and no `pool/@src` or `.ktx2` file is opened.

## Repository layout

- `include/g3d/model.hpp` — public C++ API
- `src/model.cpp` — tokenizer, collection, structure, ids and references
- `src/validate.cpp` — the semantic rules that need no bytes: domains, §17 limits, cross-element arithmetic
- `src/internal.hpp` — shared declarations between the two; not a public header
- `tools/g3d_tool.cpp` — CLI
- `tests/` — 128 checks over §1–§17, plus the seven repository samples as fixtures
- `docs/FORMAT.md` — draft specification
- `docs/BINARY.md` — binary pool (`.bin`) and package envelope (`.g3dp`) layout
- `samples/v1/` — canonical v1.0 sample exercising every core section
- `samples/primitives/` — Triangle, Square, Sphere, Cylinder, Torus, and Flat Plane

See [docs/FORMAT.md](docs/FORMAT.md) for coordinates, geometry streams, materials, animation, collision, references, and auxiliary-file rules, and [docs/BINARY.md](docs/BINARY.md) for the byte-level pool and package layouts.

## Versioning

The main document declares its version with `<g3d version="1.0">`. The old `<?g3d ... ?>` processing-instruction draft is superseded.

## License

See existing repository/sample license files. A project-wide license has not yet been selected.
