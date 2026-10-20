# G3D — Geometry Three-Dimensional

G3D is a 3D model format based on readable XML and auxiliary files for geometry, materials, textures, animations, and collision data. This repository provides an early C++20 library and command-line validator.

> **Development status:** early implementation. The parser targets the draft v1.0 root `<g3d version="1.0">`. It is not yet a complete XML 1.0 processor, schema validator, renderer, or importer.

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

The parser checks the root/version, basic tag and entity syntax, key asset fields, top-level section names, and required presence of `scene`. It records external section `src` paths but does not load those files. It is not yet a complete semantic validator.

## Repository layout

- `include/g3d/model.hpp` — public C++ API
- `src/model.cpp` — parser and structural validation
- `tools/g3d_tool.cpp` — CLI
- `tests/` — parser smoke tests
- `docs/FORMAT.md` — draft specification
- `samples/primitives/` — Triangle, Square, Sphere, Cylinder, Torus, and Flat Plane

See [docs/FORMAT.md](docs/FORMAT.md) for coordinates, geometry streams, materials, animation, collision, references, and auxiliary-file rules. Binary sidecar layouts are not stable yet.

## Versioning

The main document declares its version with `<g3d version="1.0">`. The old `<?g3d ... ?>` processing-instruction draft is superseded.

## License

See existing repository/sample license files. A project-wide license has not yet been selected.
