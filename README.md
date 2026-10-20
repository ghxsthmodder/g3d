# G3D — Geometry Three-Dimensional

G3D is a 3D asset/model format with an XML document as its human-readable manifest and auxiliary files for mesh data, animations, materials, collisions, and assets. This repository also provides a C++20 library and command-line tools.

> **Development status:** early implementation. The current validator supports a deliberately limited XML subset and basic G3D structural checks. It is not yet a complete XML 1.0 validator or a renderer/importer.

## Goals

- Keep model metadata readable and editable.
- Store large geometry and binary payloads in external sidecar files.
- Make references explicit and validate them before loading.
- Keep the core independent of graphics APIs and game engines.
- Provide a small CLI that can run in development environments, including Termux.

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

The validator checks basic XML syntax, the `model` root, unique asset IDs, and references such as `assetId`. Existing sample files are retained as historical design examples and may not pass validation yet.

## Initial file layout

- `include/g3d/model.hpp` — public C++ API
- `src/model.cpp` — parser and structural validation
- `tools/g3d_tool.cpp` — command-line utility
- `tests/` — smoke tests
- `docs/FORMAT.md` — initial format contract
- `samples/` — existing examples, preserved

## Format direction

The main `.g3d` file contains metadata and references. Heavy data should live in sidecars (for example, mesh buffers) rather than base64 embedded in XML. Paths are resolved relative to the model file by default. Binary sidecar layouts will be specified separately before being treated as stable.

## Compatibility and versioning

The `<?g3d ... ?>` processing instruction identifies the G3D document version. The first implementation targets version `1.0.0` documents for structural inspection, but schema and binary compatibility rules are still under development.

## License

See the existing repository/sample license files. A project-wide license has not yet been selected.
