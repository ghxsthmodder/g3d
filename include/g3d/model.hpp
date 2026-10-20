#pragma once
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace g3d {
struct Diagnostic {
 enum class Severity { warning, error };
 Severity severity{Severity::error};
 std::size_t line{1};
 std::string message;
};

/// A number the document states about an element, keyed as `<child>@<attr>` —
/// `yfov`, `color.r`, `extent` — so a rule can look it up without re-reading the XML.
/// `text` carries the raw value for the list-valued attributes (`extent="1 2 3"`).
struct NamedValue {
 std::string key;
 double value{0};
 std::string text;
 bool has_number{false};
 std::size_t line{1};
};

/// One `count`/`view`/`componentType` declaration, wherever it appears: a geometry
/// stream, a morph target stream, inverse bind matrices, or an animation input/output.
/// An empty `view` means the ASCII authoring mode, where the values are the element's
/// text and `component_type` is implicit (`float32`).
struct Stream {
 std::string name;
 std::string view;
 std::string component_type;
 long long count{-1};
 long long byte_offset{0};
 bool normalized{false};
 bool has_quantization{false};
 std::vector<NamedValue> quantization; // <quantization> @offset @scale @min @max
 long long ascii_numbers{0};           // values found in the element's text node
 std::string topology;
 std::size_t line{1};
};

struct Asset {
 std::string id;
 std::string type;
 std::string file; // source path from v1.0 src attribute
 std::string kind{"2d"};
 bool relative_path{true};
 std::size_t line{1};
};

/// One `<image>` candidate inside a `<textureSet>`.
struct TextureVariant {
 std::string asset_id;
 std::string vk_format;
 std::string when;
 std::string prefer;
 bool has_default{false};
 bool is_default{false};
 std::size_t line{1};
};

struct TextureSet {
 std::string id;
 std::vector<TextureVariant> images;
 std::size_t line{1};
};

struct Geometry {
 std::string id;
 std::vector<Stream> streams;
 bool has_morph{false};
 std::vector<Stream> morph_streams; // targetPositions, targetNormals
 long long morph_count{0};
 std::vector<std::string> morph_targets;
 std::size_t morph_line{1};
 std::size_t line{1};
};

struct Primitive {
 std::string id;
 std::string geometry;
 std::string material;
 std::string skin;
 long long morph_weight_count{0};
 std::size_t line{1};
};

struct Buffer {
 std::string id;
 long long byte_offset{-1};
 long long byte_length{-1};
 long long align{1};
 std::size_t line{1};
};

struct BufferView {
 std::string id;
 std::string buffer;
 long long byte_offset{-1};
 long long byte_length{-1};
 long long byte_stride{0};
 std::string target{"arrayBuffer"};
 std::size_t line{1};
};

struct Joint {
 std::string id;
 std::string parent;
 bool has_parent{false};
 std::size_t line{1};
};

struct Skeleton {
 std::string id;
 std::vector<std::string> nodes; // @nodes, normative order
 std::vector<Joint> joints;
 bool has_inverse_bind{false};
 Stream inverse_bind;
 std::size_t line{1};
};

struct Bind {
 std::string node;
 std::string mode{"linear"};
 std::size_t line{1};
};

struct Skin {
 std::string id;
 std::string skeleton;
 std::string max_influences;
 std::vector<Bind> binds;
 std::size_t line{1};
};

/// One `<texture>` in a material slot.
struct TextureSlot {
 std::string asset_id;
 std::string colorspace;
 std::string slot; // enclosing element: baseColor, normal, …
 long long tex_coord{-1};
 long long layer{-1};
 std::size_t line{1};
};

struct Material {
 std::string id;
 std::string name;
 std::string alpha_mode{"opaque"};
 std::string workflow{"metallicRoughness"};
 bool has_alpha_cutoff{false};
 std::vector<TextureSlot> slots;
 std::vector<NamedValue> values; // slot scalars: baseColor.r, occlusion.strength, …
 std::size_t line{1};
};

struct Camera {
 std::string id;
 std::string type;
 std::vector<NamedValue> values;
 std::size_t line{1};
};

struct Light {
 std::string id;
 std::string type;
 std::vector<NamedValue> values;
 std::size_t line{1};
};

struct Sampler {
 std::string id;
 Stream input;
 Stream output;
 bool has_input{false};
 bool has_output{false};
 std::size_t line{1};
};

struct Channel {
 std::string target;
 std::string property;
 std::string sampler;
 std::string interpolation{"linear"};
 std::size_t line{1};
};

struct Animation {
 std::string id;
 bool has_duration{false};
 double duration{0};
 std::vector<Sampler> samplers;
 std::vector<Channel> channels;
 std::size_t line{1};
};

struct Shape {
 std::string type;
 std::string geometry;
 std::string kind; // mesh shapes: convex | staticTriangleMesh
 std::vector<NamedValue> values;
 std::size_t line{1};
};

struct Body {
 std::string id;
 std::string node;
 std::string type;
 std::vector<Shape> shapes;
 std::size_t line{1};
};

struct Node {
 std::string id;
 std::string mesh_ref;
 std::vector<std::string> camera_refs;
 std::vector<std::string> light_refs;
 std::vector<NamedValue> values; // @translation @rotation @scale
 std::size_t depth{1};
 std::size_t line{1};
};

struct Document {
 std::filesystem::path source_path;
 std::string version;
 std::size_t max_depth{0}; // deepest element nesting reached, bounded by FORMAT.md §17
 std::vector<Asset> assets;
 std::vector<TextureSet> texture_sets;
 std::vector<Geometry> geometries;
 std::vector<Primitive> primitives;
 std::vector<Buffer> buffers;
 std::vector<BufferView> views;
 std::vector<Skeleton> skeletons;
 std::vector<Skin> skins;
 std::vector<Material> materials;
 std::vector<Camera> cameras;
 std::vector<Light> lights;
 std::vector<Animation> animations;
 std::vector<Body> bodies;
 std::vector<Node> nodes;
 std::vector<std::string> external_files;
 std::vector<Diagnostic> diagnostics;
 [[nodiscard]] bool valid() const noexcept;
};
/// Parse G3D v1.0: structure (root, section order, ids, references) and the semantic
/// rules that need no bytes — value domains, §17 limits, cross-element arithmetic. This
/// parser supports a restricted XML subset and does not load external section files or
/// the pool. A valid result does not imply the pool or the `.ktx2` payloads conform.
[[nodiscard]] Document parse(std::string_view xml, std::filesystem::path source_path = {});
/// Read a document from disk; I/O errors are returned as diagnostics.
[[nodiscard]] Document load(const std::filesystem::path& path);
} // namespace g3d
