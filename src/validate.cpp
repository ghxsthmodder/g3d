#include "g3d/model.hpp"

#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <climits>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace g3d {
namespace {

// FORMAT.md §17. Provisional pending the owner's confirmation, but a validator cannot
// test an omitted threshold, so they are enforced as stated.
constexpr long long kMaxPositions = 16'777'215; // 24-bit index ceiling
constexpr long long kMaxGeometries = 65'535;
constexpr long long kMaxPrimitives = 65'535;
constexpr long long kMaxNodes = 65'535;
constexpr long long kMaxJoints = 65'535;
constexpr long long kMaxTextures = 256;
constexpr long long kMaxSetMembers = 8;
constexpr long long kMaxMorphTargets = 8;
constexpr double kPiOverTwo = 1.5707963267948966;
constexpr double kQuaternionTolerance = 1e-4; // §4, shared by weights0
constexpr double kQuantizationTolerance = 1e-6;
// Above any real pool, below the point where summing three document-supplied offsets
// could overflow. BINARY.md §3 makes that sum the attack, so the bound is checked first.
constexpr long long kSaneBytes = 1LL << 60;

const NamedValue* find_value(const std::vector<NamedValue>& values, const std::string& key) {
    const auto found = std::find_if(values.begin(), values.end(),
                                    [&](const NamedValue& v) { return v.key == key; });
    return found == values.end() ? nullptr : &*found;
}

bool number_value(const std::vector<NamedValue>& values, const std::string& key, double& out) {
    const auto* found = find_value(values, key);
    if (!found || !found->has_number) return false;
    out = found->value;
    return true;
}

/// A whitespace-separated numeric array (FORMAT.md §6). Returns false if any element is
/// not a finite number, so a caller can report once rather than per token.
bool split_numbers(const std::string& text, std::vector<double>& out) {
    out.clear();
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const auto begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (begin == i) break;
        double value = 0;
        if (!number_text(text.substr(begin, i - begin), value)) return false;
        out.push_back(value);
    }
    return !out.empty();
}

template <std::size_t N>
bool in_domain(const std::string& value, const std::array<const char*, N>& domain) {
    return std::find(domain.begin(), domain.end(), value) != domain.end();
}

void require_domain(Document& doc, std::size_t line, std::string_view element, std::string_view attribute,
                    const std::string& value, bool present, const std::vector<std::string>& allowed) {
    if (!present) return;
    if (std::find(allowed.begin(), allowed.end(), value) != allowed.end()) return;
    std::string list;
    for (const auto& candidate : allowed) { if (!list.empty()) list += ", "; list += candidate; }
    report(doc, line, "<" + std::string(element) + "> attribute '" + std::string(attribute) +
                      "' must be one of: " + list + " (got '" + value + "')");
}

std::string join(const std::vector<std::string>& parts) {
    std::string list;
    for (const auto& part : parts) { if (!list.empty()) list += ", "; list += part; }
    return list;
}

bool is_ident_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '.' || c == '-' || c == '/';
}

/// The closed capability grammar of FORMAT.md §8.4. Anything outside it is an error: the
/// point of a closed grammar is that satisfiability is decidable without a device.
/// `formatSupported(NAME)` and `hasExtension("literal")` take different argument shapes,
/// which is what keeps a typo in a codec name from parsing as a valid condition.
struct WhenParser {
    std::vector<std::string> tokens;
    std::size_t position{0};

    bool parse() { return parse_or() && position == tokens.size(); }

    bool at(const char* token) const {
        return position < tokens.size() && tokens[position] == token;
    }

    bool parse_or() {
        if (!parse_and()) return false;
        while (at("or")) { ++position; if (!parse_and()) return false; }
        return true;
    }

    bool parse_and() {
        if (!parse_unary()) return false;
        for (;;) {
            if (at("and")) { ++position; if (!parse_unary()) return false; continue; }
            if (starts_primary()) { if (!parse_unary()) return false; continue; } // implicit AND
            return true;
        }
    }

    bool parse_unary() {
        if (at("not")) { ++position; return parse_unary(); }
        return parse_primary();
    }

    bool starts_primary() const {
        if (position >= tokens.size()) return false;
        const auto& token = tokens[position];
        return token == "*" || token == "(" || token == "not" || token == "formatSupported" ||
               token == "apiLevel" || token == "hasExtension";
    }

    bool parse_argument(bool quoted) {
        if (position >= tokens.size()) return false;
        const auto& argument = tokens[position++];
        if (quoted) return argument.size() > 2 && argument.front() == '"' && argument.back() == '"';
        if (argument.empty() || argument.front() == '"') return false;
        return std::all_of(argument.begin(), argument.end(), is_ident_char);
    }

    bool parse_primary() {
        if (position >= tokens.size()) return false;
        const auto& token = tokens[position];
        if (token == "*") { ++position; return true; }
        if (token == "(") {
            ++position;
            if (!parse_or()) return false;
            if (!at(")")) return false;
            ++position;
            return true;
        }
        if (token == "formatSupported" || token == "hasExtension") {
            const bool quoted = token == "hasExtension";
            ++position;
            if (!at("(")) return false;
            ++position;
            if (!parse_argument(quoted)) return false;
            if (!at(")")) return false;
            ++position;
            return true;
        }
        if (token == "apiLevel") {
            ++position;
            if (!at(">=")) return false;
            ++position;
            if (position >= tokens.size()) return false;
            const auto& value = tokens[position++];
            return !value.empty() && std::all_of(value.begin(), value.end(),
                                                 [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
        }
        return false;
    }
};

std::vector<std::string> tokenize_when(const std::string& text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == '(' || c == ')' || c == '*') { out.emplace_back(1, c); ++i; continue; }
        if (text.compare(i, 2, ">=") == 0) { out.push_back(">="); i += 2; continue; }
        if (c == '"' || c == '\'') {
            const auto close = text.find(c, i + 1);
            if (close == std::string::npos) { out.emplace_back(1, '\0'); break; }
            out.push_back(text.substr(i, close - i + 1));
            i = close + 1;
            continue;
        }
        const auto begin = i;
        while (i < text.size() && (is_ident_char(text[i]) || text[i] == '=' || text[i] == '>')) ++i;
        if (i == begin) { out.emplace_back(1, '\0'); break; }
        out.push_back(text.substr(begin, i - begin));
    }
    return out;
}

bool when_dsl_valid(const std::string& text) {
    WhenParser parser{tokenize_when(text)};
    return parser.parse();
}

const Stream* stream_of(const Geometry& geometry, const std::string& name) {
    const auto found = std::find_if(geometry.streams.begin(), geometry.streams.end(),
                                    [&](const Stream& s) { return s.name == name; });
    return found == geometry.streams.end() ? nullptr : &*found;
}

const Geometry* find_geometry(const Document& doc, const std::string& id) {
    const auto found = std::find_if(doc.geometries.begin(), doc.geometries.end(),
                                    [&](const Geometry& g) { return g.id == id; });
    return found == doc.geometries.end() ? nullptr : &*found;
}

const BufferView* find_view(const Document& doc, const std::string& id) {
    const auto found = std::find_if(doc.views.begin(), doc.views.end(),
                                    [&](const BufferView& v) { return v.id == id; });
    return found == doc.views.end() ? nullptr : &*found;
}

const Buffer* find_buffer(const Document& doc, const std::string& id) {
    const auto found = std::find_if(doc.buffers.begin(), doc.buffers.end(),
                                    [&](const Buffer& b) { return b.id == id; });
    return found == doc.buffers.end() ? nullptr : &*found;
}

/// FORMAT.md §10 components. `colors0` is 3 or 4, so a stream may match any option.
std::vector<long long> components_options(const std::string& name) {
    if (name == "positions" || name == "normals" || name == "targetPositions" || name == "targetNormals") return {3};
    if (name == "tangents" || name == "joints0" || name == "weights0") return {4};
    if (name == "uv0" || name == "uv1") return {2};
    if (name == "colors0") return {3, 4};
    if (name == "indices") return {1};
    if (name == "inverseBindMatrices") return {16};
    return {};
}

long long component_size(const std::string& component_type) {
    if (component_type == "int8" || component_type == "uint8") return 1;
    if (component_type == "int16" || component_type == "uint16") return 2;
    if (component_type == "float32" || component_type == "uint32") return 4;
    return 0;
}

bool is_power_of_two(long long value) { return value > 0 && (value & (value - 1)) == 0; }

/// (count - 1) * step + elementSize + byteOffset, with every intermediate checked. The
/// offsets and counts are document-supplied, and BINARY.md §3 names this overflow as the
/// classic way a validator reads past the end of a mapping.
bool element_span(long long count, long long step, long long element_size, long long byte_offset,
                  long long& out) {
    if (count < 1 || step < 0 || element_size < 0 || byte_offset < 0) return false;
    const auto gaps = count - 1;
    if (gaps > 0 && step > (LLONG_MAX - element_size) / gaps) return false;
    const auto last = gaps * step + element_size;
    if (last > LLONG_MAX - byte_offset) return false;
    out = last + byte_offset;
    return true;
}

void check_component_type(Document& doc, std::size_t line, const std::string& element,
                          const std::string& value, bool present, std::string_view owner,
                          const std::vector<std::string>& allowed) {
    if (!present) return;
    static const std::array<const char*, 6> v1_types = {"float32", "int8", "uint8", "int16", "uint16", "uint32"};
    if (!in_domain(value, v1_types)) {
        std::string list;
        for (const auto& type : v1_types) { if (!list.empty()) list += ", "; list += type; }
        report(doc, line, "<" + element + "> componentType '" + value + "' is not a v1.0 type; allowed: " + list +
                          " (BINARY.md §5)");
        return;
    }
    if (std::find(allowed.begin(), allowed.end(), value) == allowed.end())
        report(doc, line, "<" + element + "> of " + std::string(owner) + " must not use componentType '" +
                          value + "'; allowed here: " + join(allowed));
}

} // namespace

void validate(Document& doc); // matches the declaration in internal.hpp

namespace {

/// FORMAT.md §2: `[A-Za-z_][A-Za-z0-9_.-]*`. An id outside it cannot be quoted safely in
/// the whitespace-separated lists (`skeleton/@nodes`, `morphWeights`) or matched by the
/// `when` DSL's identifier rule, so a malformed id is a malformed document, not a style note.
bool id_shape(const std::string& id) {
    const auto head = static_cast<unsigned char>(id.front());
    if (!std::isalpha(head) && id.front() != '_') return false;
    for (std::size_t i = 1; i < id.size(); ++i) {
        const auto c = static_cast<unsigned char>(id[i]);
        if (!(std::isalnum(c) || id[i] == '_' || id[i] == '.' || id[i] == '-')) return false;
    }
    return true;
}

void validate_ids(Document& doc) {
    auto check = [&](const std::string& id, std::size_t line, std::string_view element) {
        // An absent id is already an error at the parser; reporting it twice is noise.
        if (!id.empty() && !id_shape(id))
            report(doc, line, "<" + std::string(element) + "> id must match [A-Za-z_][A-Za-z0-9_.-]*: " + id +
                              " (FORMAT.md §2)");
    };
    for (const auto& asset : doc.assets) check(asset.id, asset.line, "asset");
    for (const auto& set : doc.texture_sets) check(set.id, set.line, "textureSet");
    for (const auto& buffer : doc.buffers) check(buffer.id, buffer.line, "buffer");
    for (const auto& view : doc.views) check(view.id, view.line, "bufferView");
    for (const auto& geometry : doc.geometries) {
        check(geometry.id, geometry.line, "geometry");
        for (const auto& target : geometry.morph_targets) check(target, geometry.morph_line, "target");
    }
    for (const auto& primitive : doc.primitives) check(primitive.id, primitive.line, "primitive");
    for (const auto& skeleton : doc.skeletons) {
        check(skeleton.id, skeleton.line, "skeleton");
        for (const auto& joint : skeleton.joints) check(joint.id, joint.line, "joint");
    }
    for (const auto& skin : doc.skins) check(skin.id, skin.line, "skin");
    for (const auto& material : doc.materials) check(material.id, material.line, "material");
    for (const auto& camera : doc.cameras) check(camera.id, camera.line, "camera");
    for (const auto& light : doc.lights) check(light.id, light.line, "light");
    for (const auto& animation : doc.animations) {
        check(animation.id, animation.line, "animation");
        for (const auto& sampler : animation.samplers) check(sampler.id, sampler.line, "sampler");
    }
    for (const auto& body : doc.bodies) check(body.id, body.line, "body");
    for (const auto& node : doc.nodes) check(node.id, node.line, "node");
}

void validate_textures(Document& doc) {
    static const std::vector<std::string> kinds = {"2d", "cube", "2dArray"};
    static const std::vector<std::string> colorspaces = {"srgb", "linear"};

    std::set<std::string> referenced;
    for (const auto& set : doc.texture_sets)
        for (const auto& image : set.images) referenced.insert(image.asset_id);
    for (const auto& material : doc.materials)
        for (const auto& slot : material.slots) referenced.insert(slot.asset_id);

    for (const auto& asset : doc.assets) {
        require_domain(doc, asset.line, "asset", "kind", asset.kind, true, kinds);
        if (asset.file.size() < 5 || asset.file.substr(asset.file.size() - 5) != ".ktx2")
            report(doc, asset.line, "asset '" + asset.id + "' is not a .ktx2 file; a published package "
                                    "must carry KTX2 (FORMAT.md §8.1)", Diagnostic::Severity::warning);
        if (!referenced.contains(asset.id))
            report(doc, asset.line, "asset '" + asset.id + "' is named by no textureSet and by no material slot",
                   Diagnostic::Severity::warning);
    }

    for (const auto& set : doc.texture_sets) {
        if (set.images.empty()) report(doc, set.line, "<textureSet id=\"" + set.id + "\"> declares no <image> variant");
        if (static_cast<long long>(set.images.size()) > kMaxSetMembers)
            report(doc, set.line, "<textureSet id=\"" + set.id + "\"> exceeds the limit of " +
                                  std::to_string(kMaxSetMembers) + " variants (FORMAT.md §17)");
        int defaults = 0;
        for (const auto& image : set.images) {
            if (image.asset_id.empty()) report(doc, image.line, "<image> requires assetId");
            if (image.vk_format.rfind("VK_FORMAT_", 0) != 0)
                report(doc, image.line, "<image> vkFormat must be a VK_FORMAT_* token, got '" + image.vk_format + "'");
            if (image.when.empty()) report(doc, image.line, "<image> requires a when condition ('*' is the always-true one)");
            else if (!when_dsl_valid(image.when))
                report(doc, image.line, "<image> when is not in the capability grammar of FORMAT.md §8.4: '" + image.when + "'");
            if (!image.prefer.empty() && image.prefer.find_first_not_of("-+0123456789") != std::string::npos)
                report(doc, image.line, "<image> prefer must be an integer: '" + image.prefer + "'");
            if (image.has_default && image.is_default) ++defaults;
            const auto found = std::find_if(doc.assets.begin(), doc.assets.end(),
                                            [&](const Asset& a) { return a.id == image.asset_id; });
            if (found != doc.assets.end() && found->type != "texture")
                report(doc, image.line, "<image> assetId '" + image.asset_id + "' names an asset that is not a texture");
        }
        if (defaults != 1)
            report(doc, set.line, "<textureSet id=\"" + set.id + "\" needs exactly one default=\"true\" variant (found " +
                                  std::to_string(defaults) + ")");
    }

    auto kind_of = [&](const std::string& id) -> std::string {
        for (const auto& asset : doc.assets) if (asset.id == id) return asset.kind;
        for (const auto& set : doc.texture_sets) {
            if (set.id != id) continue;
            std::string result;
            for (const auto& image : set.images)
                for (const auto& asset : doc.assets)
                    if (asset.id == image.asset_id) {
                        if (result.empty()) result = asset.kind;
                        else if (result != asset.kind) return "mixed";
                    }
            return result;
        }
        return {};
    };

    for (const auto& material : doc.materials) {
        for (const auto& value : material.values)
            if (value.key == "colorspace")
                require_domain(doc, value.line, "material", "colorspace", value.text, true, colorspaces);
        for (const auto& slot : material.slots) {
            require_domain(doc, slot.line, "texture", "colorspace", slot.colorspace,
                           !slot.colorspace.empty(), colorspaces);
            // Only uv0 and uv1 exist (§10), so a slot may not address a third set.
            if (slot.tex_coord > 1)
                report(doc, slot.line, "<texture> texCoord is " + std::to_string(slot.tex_coord) +
                                       " but v1.0 defines uv0 and uv1 only (indices 0 and 1)");
            if (slot.layer >= 0) {
                const auto kind = kind_of(slot.asset_id);
                if (kind != "2dArray")
                    report(doc, slot.line, "<texture layer> requires a kind=\"2dArray\" image; '" + slot.asset_id +
                                           "' resolves to kind '" + (kind.empty() ? "unknown" : kind) +
                                           "' (FORMAT.md §8.3 rule 7)");
            }
            if (!slot.colorspace.empty()) {
                std::vector<std::string> formats;
                for (const auto& set : doc.texture_sets)
                    if (set.id == slot.asset_id)
                        for (const auto& image : set.images) formats.push_back(image.vk_format);
                for (const auto& format : formats) {
                    const bool srgb = format.find("SRGB") != std::string::npos;
                    const bool linear_token = !srgb && (format.find("UNORM") != std::string::npos ||
                                                        format.find("SFLOAT") != std::string::npos ||
                                                        format.find("FLOAT") != std::string::npos);
                    if (slot.colorspace == "srgb" && !srgb)
                        report(doc, slot.line, "slot '" + slot.slot + "' declares colorspace srgb but variant '" +
                                               format + "' is not an SRGB format");
                    if (slot.colorspace == "linear" && !linear_token)
                        report(doc, slot.line, "slot '" + slot.slot + "' declares colorspace linear but variant '" +
                                               format + "' is an SRGB format");
                }
            }
        }
    }

    if (static_cast<long long>(doc.assets.size()) > kMaxTextures)
        report(doc, 1, "the document declares more than " + std::to_string(kMaxTextures) +
                       " textures (FORMAT.md §17)");
}

void check_stream_mode(Document& doc, const Stream& stream, const std::string& owner) {
    // BINARY.md §7: a stream is either a text node or a view, never both and never neither.
    if (!stream.view.empty() && stream.ascii_numbers > 0)
        report(doc, stream.line, "<" + stream.name + "> of " + owner + " names view='" + stream.view +
                                 "' and also carries " + std::to_string(stream.ascii_numbers) +
                                 " values as text (BINARY.md §7)");
    if (stream.view.empty() && stream.ascii_numbers == 0)
        report(doc, stream.line, "<" + stream.name + "> of " + owner + " has neither a view= nor values as text "
                                 "(BINARY.md §7)");
    if (stream.view.empty() && stream.byte_offset != 0)
        report(doc, stream.line, "<" + stream.name + "> of " + owner + " is an ASCII array, so byteOffset has no meaning");
    if (!stream.view.empty() || stream.count < 1) return;

    // ASCII mode carries its own length: count is entries, the table in FORMAT.md §10
    // fixes the components, and the text must be exactly count × components numbers.
    const auto options = components_options(stream.name);
    if (options.empty()) return; // input/output element size depends on the animated property
    bool matched = false;
    for (const auto components : options)
        if (stream.ascii_numbers == stream.count * components) matched = true;
    if (!matched) {
        std::string expected;
        for (const auto components : options) {
            if (!expected.empty()) expected += " or ";
            expected += std::to_string(stream.count * components);
        }
        report(doc, stream.line, "<" + stream.name + "> of " + owner + " lists " +
                                 std::to_string(stream.ascii_numbers) + " values but count=" +
                                 std::to_string(stream.count) + " needs " + expected + " (FORMAT.md §10)");
    }
}

void check_stream_storage(Document& doc, const Stream& stream) {
    if (stream.view.empty() || stream.count < 1) return;
    const auto* view = find_view(doc, stream.view);
    if (!view) return; // the reference itself was already reported
    const auto options = components_options(stream.name);
    const auto size = component_size(stream.component_type);
    if (options.empty() || size == 0) return;
    const auto* buffer = find_buffer(doc, view->buffer);
    for (const auto components : options) {
        const auto element_size = components * size;
        const auto step = view->byte_stride != 0 ? view->byte_stride : element_size;
        long long span = 0;
        if (!element_span(stream.count, step, element_size, stream.byte_offset, span)) {
            report(doc, stream.line, "<" + stream.name + "> offset arithmetic overflows a 64-bit length "
                                     "(BINARY.md §3)");
            continue;
        }
        if (view->byte_length >= 1 && span > view->byte_length)
            report(doc, stream.line, "<" + stream.name + "> reaches byte " + std::to_string(span) +
                                     " of view '" + view->id + "', which is " + std::to_string(view->byte_length) +
                                     " bytes long (BINARY.md §7)");
        if (!buffer || buffer->byte_offset > kSaneBytes || view->byte_offset > kSaneBytes ||
            stream.byte_offset > kSaneBytes)
            continue;
        const auto absolute = buffer->byte_offset + view->byte_offset + stream.byte_offset;
        if (size > 1 && absolute % size != 0)
            report(doc, stream.line, "<" + stream.name + "> starts at byte " + std::to_string(absolute) +
                                     ", not a multiple of its " + std::to_string(size) + "-byte component "
                                     "(BINARY.md §4; a stride misaligned this way is decodable, so it is a warning)",
                   Diagnostic::Severity::warning);
    }
}

void validate_quantization(Document& doc, const Stream& positions) {
    static const std::array<const char*, 4> keys = {"offset", "scale", "min", "max"};
    std::array<std::vector<double>, 4> parts;
    bool parsed = true;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto* value = find_value(positions.quantization, keys[index]);
        if (!value) {
            report(doc, positions.line, std::string("<quantization> requires ") + keys[index]);
            parsed = false;
            continue;
        }
        if (!split_numbers(value->text, parts[index]) || parts[index].size() != 3) {
            report(doc, value->line, std::string("<quantization> ") + keys[index] +
                                     " must be three numbers: " + value->text);
            parsed = false;
        }
    }
    if (!parsed) return;
    const auto& offset = parts[0];
    const auto& scale = parts[1];
    const auto& minimum = parts[2];
    const auto& maximum = parts[3];
    for (int axis = 0; axis < 3; ++axis) {
        const auto tolerance = kQuantizationTolerance *
                               std::max({std::abs(minimum[axis]), std::abs(maximum[axis]), 1.0});
        if (std::abs(minimum[axis] - offset[axis]) > tolerance)
            report(doc, positions.line, "quantization min[" + std::to_string(axis) + "] is " +
                                       std::to_string(minimum[axis]) + " but offset is " + std::to_string(offset[axis]) +
                                       "; the box and the map must agree (BINARY.md §6)");
        const auto far = offset[axis] + scale[axis];
        if (std::abs(maximum[axis] - far) > tolerance)
            report(doc, positions.line, "quantization max[" + std::to_string(axis) + "] is " +
                                       std::to_string(maximum[axis]) + " but offset+scale is " + std::to_string(far) +
                                       "; the box and the map must agree (BINARY.md §6)");
    }
}

void validate_geometry(Document& doc) {
    static const std::vector<std::string> topologies = {"triangles"};
    struct TypeRule { const char* name; std::vector<std::string> allowed; bool never_normalized; bool must_normalize; };
    const std::vector<TypeRule> rules = {
        {"positions", {"float32", "uint16"}, false, false},
        {"normals", {"float32", "int8", "int16"}, false, false},
        {"tangents", {"float32", "int8", "int16"}, false, false},
        {"uv0", {"float32", "uint16"}, false, false},
        {"uv1", {"float32", "uint16"}, false, false},
        {"colors0", {"float32", "uint8", "uint16"}, false, false},
        {"joints0", {"uint8", "uint16"}, true, false},
        {"weights0", {"uint16"}, false, true},
        {"indices", {"uint16", "uint32"}, true, false},
    };
    auto rule_for = [&](const std::string& name) -> const TypeRule* {
        const auto found = std::find_if(rules.begin(), rules.end(),
                                        [&](const TypeRule& r) { return name == r.name; });
        return found == rules.end() ? nullptr : &*found;
    };

    if (static_cast<long long>(doc.geometries.size()) > kMaxGeometries)
        report(doc, 1, "the document declares more than " + std::to_string(kMaxGeometries) +
                       " geometries (FORMAT.md §17)");
    if (static_cast<long long>(doc.primitives.size()) > kMaxPrimitives)
        report(doc, 1, "the document declares more than " + std::to_string(kMaxPrimitives) +
                       " primitives (FORMAT.md §17)");

    for (const auto& geometry : doc.geometries) {
        const std::string owner = "geometry id=\"" + geometry.id + "\"";
        const auto* positions = stream_of(geometry, "positions");
        const auto* indices = stream_of(geometry, "indices");
        if (!positions) report(doc, geometry.line, "<" + owner + "> declares no <positions> stream");
        if (!indices) report(doc, geometry.line, "<" + owner + "> declares no <indices> stream");

        for (const auto& stream : geometry.streams) {
            const auto* type_rule = rule_for(stream.name);
            if (!type_rule) continue;
            const bool pooled = !stream.view.empty();
            if (stream.count < 0) report(doc, stream.line, "<" + stream.name + "> requires count");
            if (pooled && stream.component_type.empty())
                report(doc, stream.line, "<" + stream.name + "> of " + owner +
                                         " uses a view, so componentType is required");
            check_component_type(doc, stream.line, stream.name, stream.component_type,
                                 !stream.component_type.empty(), owner, type_rule->allowed);
            if (stream.normalized && stream.component_type.empty())
                report(doc, stream.line, "<" + stream.name + "> of " + owner +
                                         " declares normalized without componentType; ASCII arrays are float32");
            if (stream.normalized && type_rule->never_normalized)
                report(doc, stream.line, "<" + stream.name + "> must never be normalized (FORMAT.md §10)");
            if (pooled && type_rule->must_normalize && !stream.normalized)
                report(doc, stream.line, "<" + stream.name + "> of " + owner + " must be normalized=\"true\" (FORMAT.md §9)");
            if (!stream.topology.empty())
                require_domain(doc, stream.line, stream.name, "topology", stream.topology, true, topologies);
            check_stream_mode(doc, stream, owner);
            check_stream_storage(doc, stream);
        }

        if (positions) {
            if (positions->count >= 0 && positions->count < 3)
                report(doc, positions->line, "positions.count must be at least 3 (got " + std::to_string(positions->count) + ")");
            if (positions->count > kMaxPositions)
                report(doc, positions->line, "positions.count exceeds the limit of " + std::to_string(kMaxPositions) +
                                             " (FORMAT.md §17)");
            if (positions->component_type == "uint16" && !positions->has_quantization)
                report(doc, positions->line, "uint16 positions require <quantization> (FORMAT.md §10, BINARY.md §6)");
            if (positions->has_quantization && positions->component_type != "uint16")
                report(doc, positions->line, "<quantization> is only defined for uint16 positions");
            if (positions->has_quantization) validate_quantization(doc, *positions);
            for (const auto& stream : geometry.streams) {
                if (stream.name == "positions" || stream.name == "indices") continue;
                if (stream.count >= 0 && positions->count >= 0 && stream.count != positions->count)
                    report(doc, stream.line, "<" + stream.name + ">.count is " + std::to_string(stream.count) +
                                             " but positions.count is " + std::to_string(positions->count) +
                                             "; every vertex stream shares it (FORMAT.md §10)");
            }
        }
        if (indices && indices->count >= 0) {
            if (indices->count < 3) report(doc, indices->line, "indices.count must be at least 3");
            if (indices->count % 3 != 0)
                report(doc, indices->line, "indices.count must be a multiple of 3 (triangles, FORMAT.md §10)");
            if (indices->count > kMaxPositions)
                report(doc, indices->line, "indices.count exceeds the limit of " + std::to_string(kMaxPositions) +
                                           " (FORMAT.md §17)");
        }

        if (!geometry.has_morph) continue;
        if (geometry.morph_count < 1)
            report(doc, geometry.morph_line, "<morph> count must be at least 1");
        if (geometry.morph_count > kMaxMorphTargets)
            report(doc, geometry.morph_line, "<morph> count exceeds the limit of " + std::to_string(kMaxMorphTargets) +
                                             " targets (FORMAT.md §17)");
        if (geometry.morph_count >= 1 &&
            static_cast<long long>(geometry.morph_targets.size()) != geometry.morph_count)
            report(doc, geometry.morph_line, "<morph> declares " + std::to_string(geometry.morph_targets.size()) +
                                             " <target> elements for count=" + std::to_string(geometry.morph_count));
        const auto has_targets = std::any_of(geometry.morph_streams.begin(), geometry.morph_streams.end(),
                                            [](const Stream& s) { return s.name == "targetPositions"; });
        if (!has_targets) report(doc, geometry.morph_line, "<morph> requires <targetPositions>");
        for (const auto& stream : geometry.morph_streams) {
            const auto allowed = stream.name == "targetNormals"
                                     ? std::vector<std::string>{"float32", "int8", "int16"}
                                     : std::vector<std::string>{"float32"};
            check_component_type(doc, stream.line, stream.name, stream.component_type,
                                 !stream.component_type.empty(), owner, allowed);
            check_stream_mode(doc, stream, owner);
            check_stream_storage(doc, stream);
            if (positions && geometry.morph_count >= 1 && stream.count >= 0) {
                const auto expected = geometry.morph_count * positions->count;
                if (stream.count != expected)
                    report(doc, stream.line, "<" + stream.name + ">.count is " + std::to_string(stream.count) +
                                             " but dense morph storage requires " + std::to_string(expected) +
                                             " (count × positions.count, FORMAT.md §11)");
            }
        }
    }
}

void validate_pool(Document& doc) {
    static const std::vector<std::string> targets = {"arrayBuffer", "elementArrayBuffer"};
    for (const auto& buffer : doc.buffers) {
        const std::string owner = "buffer id=\"" + buffer.id + "\"";
        if (buffer.byte_offset < 0) report(doc, buffer.line, "<" + owner + "> requires byteOffset");
        if (buffer.byte_length < 1) report(doc, buffer.line, "<" + owner + "> requires a positive byteLength");
        if (!is_power_of_two(buffer.align))
            report(doc, buffer.line, "<" + owner + "> align must be a power of two (BINARY.md §4)");
        else if (buffer.byte_offset >= 0 && buffer.byte_offset % buffer.align != 0)
            report(doc, buffer.line, "<" + owner + "> byteOffset " + std::to_string(buffer.byte_offset) +
                                     " does not satisfy its own align=" + std::to_string(buffer.align) +
                                     " (BINARY.md §4)");
    }
    for (const auto& view : doc.views) {
        const std::string owner = "bufferView id=\"" + view.id + "\"";
        if (view.buffer.empty()) report(doc, view.line, "<" + owner + "> requires buffer");
        if (view.byte_offset < 0) report(doc, view.line, "<" + owner + "> requires byteOffset");
        if (view.byte_length < 1) report(doc, view.line, "<" + owner + "> requires a positive byteLength");
        require_domain(doc, view.line, "bufferView", "target", view.target, true, targets);
        const auto* buffer = find_buffer(doc, view.buffer);
        // §3 makes view/@byteOffset relative to its buffer, so the alignment a reader can
        // actually pointer-cast at is the absolute one: buffer.byteOffset + view.byteOffset.
        const long long absolute = buffer && buffer->byte_offset >= 0 && view.byte_offset >= 0 &&
                                   buffer->byte_offset <= kSaneBytes && view.byte_offset <= kSaneBytes
                                       ? buffer->byte_offset + view.byte_offset
                                       : view.byte_offset;
        if (view.byte_offset >= 0) {
            if (absolute % 4 != 0)
                report(doc, view.line, "<" + owner + "> starts at pool byte " + std::to_string(absolute) +
                                       ", but a view must sit on a multiple of 4 (BINARY.md §4)");
            if (view.target == "elementArrayBuffer" && absolute % 16 != 0)
                report(doc, view.line, "<" + owner + "> targets elementArrayBuffer, so it must start on a "
                                       "multiple of 16 (pool byte " + std::to_string(absolute) + ", BINARY.md §4)");
        }
        if (view.byte_stride < 0)
            report(doc, view.line, "<" + owner + "> byteStride must not be negative");
        else if (view.byte_stride > 0) {
            if (view.byte_stride % 4 != 0)
                report(doc, view.line, "<" + owner + "> byteStride must be a multiple of 4 (BINARY.md §4)");
            if (view.byte_length >= 1 && view.byte_stride > view.byte_length)
                report(doc, view.line, "<" + owner + "> byteStride " + std::to_string(view.byte_stride) +
                                       " exceeds its own byteLength");
        }
        if (buffer && view.byte_offset >= 0 && view.byte_length >= 1) {
            if (view.byte_offset > LLONG_MAX - view.byte_length)
                report(doc, view.line, "<" + owner + "> offset arithmetic overflows a 64-bit length (BINARY.md §3)");
            else if (view.byte_offset + view.byte_length > buffer->byte_length)
                report(doc, view.line, "<" + owner + "> ends at byte " +
                                       std::to_string(view.byte_offset + view.byte_length) + " of buffer '" +
                                       buffer->id + "', which is " + std::to_string(buffer->byte_length) +
                                       " bytes long (BINARY.md §3)");
        }
    }
}

void validate_skeleton(Document& doc) {
    for (const auto& skeleton : doc.skeletons) {
        const std::string owner = "skeleton id=\"" + skeleton.id + "\"";
        if (skeleton.joints.empty()) report(doc, skeleton.line, "<" + owner + "> declares no <joint>");
        if (skeleton.nodes.empty()) report(doc, skeleton.line, "<" + owner + "> requires a nodes list (FORMAT.md §9)");
        if (static_cast<long long>(skeleton.joints.size()) > kMaxJoints)
            report(doc, skeleton.line, "<" + owner + "> exceeds the joint limit of " +
                                       std::to_string(kMaxJoints) + " (FORMAT.md §17)");

        std::set<std::string> joint_ids;
        for (const auto& joint : skeleton.joints) {
            if (!joint_ids.insert(joint.id).second) report(doc, joint.line, "duplicate joint id: " + joint.id);
            if (joint.has_parent && !joint.parent.empty() &&
                std::none_of(skeleton.joints.begin(), skeleton.joints.end(),
                             [&](const Joint& j) { return j.id == joint.parent; }))
                report(doc, joint.line, "<joint parent=\"" + joint.parent + "\"> is not a joint of " + owner);
        }
        std::set<std::string> listed;
        for (const auto& name : skeleton.nodes) {
            if (!listed.insert(name).second)
                report(doc, skeleton.line, owner + " nodes list names '" + name + "' twice");
            if (!joint_ids.contains(name))
                report(doc, skeleton.line, owner + " nodes list names '" + name + "' which is not a declared <joint>");
        }
        // joints0 and inverseBindMatrices address @nodes positionally, so a joint missing
        // from the list is a matrix with no slot as much as an index with no matrix.
        for (const auto& joint : skeleton.joints)
            if (!listed.contains(joint.id))
                report(doc, joint.line, "joint '" + joint.id + "' is declared by " + owner +
                                        " but absent from its nodes list, so no index addresses it (FORMAT.md §9)");

        int roots = 0;
        for (const auto& joint : skeleton.joints)
            if (!(joint.has_parent && !joint.parent.empty())) ++roots;
        if (!skeleton.joints.empty() && roots != 1)
            report(doc, skeleton.line, "<" + owner + "> has " + std::to_string(roots) +
                                       " root joints; the hierarchy must be single-root (FORMAT.md §9)");

        for (const auto& joint : skeleton.joints) {
            std::set<std::string> walked;
            std::string current = joint.id;
            for (;;) {
                if (!walked.insert(current).second) {
                    report(doc, skeleton.line, "<" + owner + "> joint hierarchy contains a cycle at '" + current + "'");
                    break;
                }
                const auto found = std::find_if(skeleton.joints.begin(), skeleton.joints.end(),
                                                  [&](const Joint& j) { return j.id == current; });
                if (found == skeleton.joints.end() || !found->has_parent || found->parent.empty()) break;
                current = found->parent;
            }
        }

        if (skeleton.has_inverse_bind) {
            const auto& ibm = skeleton.inverse_bind;
            if (ibm.count >= 0 && ibm.count != static_cast<long long>(skeleton.joints.size()))
                report(doc, ibm.line, "inverseBindMatrices count is " + std::to_string(ibm.count) + " but " + owner +
                                      " declares " + std::to_string(skeleton.joints.size()) + " joints (FORMAT.md §9)");
            if (!ibm.component_type.empty() && ibm.component_type != "float32")
                report(doc, ibm.line, "inverseBindMatrices must be float32 in v1.0");
            check_stream_mode(doc, ibm, owner);
            check_stream_storage(doc, ibm);
        }
    }

    for (const auto& skin : doc.skins) {
        const std::string owner = "skin id=\"" + skin.id + "\"";
        if (skin.skeleton.empty()) report(doc, skin.line, "<" + owner + "> requires skeleton");
        if (!skin.max_influences.empty() && skin.max_influences != "1" && skin.max_influences != "2" &&
            skin.max_influences != "4")
            report(doc, skin.line, "skin maxInfluences must be 1, 2 or 4 in v1.0 (got '" + skin.max_influences + "')");
        if (skin.binds.empty())
            report(doc, skin.line, "<" + owner + "> declares no <bind>", Diagnostic::Severity::warning);
        for (const auto& bind : skin.binds) {
            if (bind.node.empty()) report(doc, bind.line, "<bind> requires node");
            if (bind.mode != "linear")
                report(doc, bind.line, "<bind mode=\"" + bind.mode + "\"> is not a v1.0 mode; only linear exists");
        }
    }

    for (const auto& primitive : doc.primitives) {
        const std::string owner = "primitive id=\"" + primitive.id + "\"";
        const auto* geometry = find_geometry(doc, primitive.geometry);
        if (!geometry) continue;
        const auto* joints = stream_of(*geometry, "joints0");
        const bool has_weights = stream_of(*geometry, "weights0") != nullptr;
        if (!primitive.skin.empty() && (!joints || !has_weights))
            report(doc, primitive.line, "<" + owner + "> names skin='" + primitive.skin + "' but geometry '" +
                                        primitive.geometry + "' lacks joints0/weights0 (FORMAT.md §9)");
        if (primitive.skin.empty() && (joints || has_weights))
            report(doc, primitive.line, "geometry '" + primitive.geometry + "' of <" + owner +
                                        "> carries joints0/weights0 without a skin (FORMAT.md §9)");
        if (primitive.morph_weight_count > 0) {
            if (!geometry->has_morph)
                report(doc, primitive.line, "<" + owner + "> gives morphWeights but geometry '" +
                                            primitive.geometry + "' declares no <morph>");
            else if (geometry->morph_count >= 1 && primitive.morph_weight_count != geometry->morph_count)
                report(doc, primitive.line, owner + " morphWeights lists " +
                                            std::to_string(primitive.morph_weight_count) +
                                            " values for a <morph count=\"" + std::to_string(geometry->morph_count) + "\">");
        }
        if (!joints || joints->component_type.empty() || primitive.skin.empty()) continue;
        const auto skin = std::find_if(doc.skins.begin(), doc.skins.end(),
                                       [&](const Skin& s) { return s.id == primitive.skin; });
        if (skin == doc.skins.end()) continue;
        const auto skeleton = std::find_if(doc.skeletons.begin(), doc.skeletons.end(),
                                           [&](const Skeleton& s) { return s.id == skin->skeleton; });
        if (skeleton == doc.skeletons.end()) continue;
        const long long cap = joints->component_type == "uint8" ? 256 : 65'536;
        if (static_cast<long long>(skeleton->joints.size()) > cap)
            report(doc, skeleton->line, "skeleton '" + skeleton->id + "' has " +
                                       std::to_string(skeleton->joints.size()) + " joints but joints0 is " +
                                       joints->component_type + ", which caps the index at " + std::to_string(cap));
    }
}

void validate_materials(Document& doc) {
    static const std::vector<std::string> alpha_modes = {"opaque", "mask", "blend"};
    for (const auto& material : doc.materials) {
        require_domain(doc, material.line, "material", "alphaMode", material.alpha_mode, true, alpha_modes);
        if (material.workflow != "metallicRoughness")
            report(doc, material.line, "<pbr workflow=\"" + material.workflow +
                                       "\"> is not a v1.0 workflow; metallicRoughness is the only one");
        if (material.has_alpha_cutoff) {
            if (material.alpha_mode != "mask")
                report(doc, material.line, "alphaCutoff on a material whose alphaMode is not 'mask' has no effect",
                       Diagnostic::Severity::warning);
            const auto* cutoff = find_value(material.values, "alphaCutoff");
            if (cutoff && !cutoff->has_number)
                report(doc, cutoff->line, "material alphaCutoff is not a number: " + cutoff->text);
        }
        if (material.alpha_mode == "mask" && !material.has_alpha_cutoff)
            report(doc, material.line, "alphaMode=\"mask\" requires alphaCutoff (FORMAT.md §12)");

        auto range = [&](const std::string& key, double low, double high, const char* why) {
            for (const auto& value : material.values) {
                if (value.key != key) continue;
                if (!value.has_number) {
                    report(doc, value.line, "material '" + key + "' is not a number: " + value.text);
                    continue;
                }
                if (value.value < low || value.value > high)
                    report(doc, value.line, "material '" + key + "' must be in [" + std::to_string(low) + ", " +
                                            std::to_string(high) + "] — " + why);
            }
        };
        // collect_values maps `value=` onto the element itself, so a scalar slot is keyed
        // by its element name: <metallic value="0"/> is "metallic".
        for (const char* component : {"baseColor.r", "baseColor.g", "baseColor.b", "baseColor.a",
                                      "emissive.r", "emissive.g", "emissive.b", "metallic", "roughness",
                                      "occlusion.strength"})
            range(component, 0.0, 1.0, "FORMAT.md §12 scalars");
        for (const auto& value : material.values)
            if (value.key == "emissive.strength" && value.has_number && value.value < 0)
                report(doc, value.line, "emissive strength must be >= 0 (FORMAT.md §12)");
    }
}

void validate_cameras_lights(Document& doc) {
    static const std::vector<std::string> camera_types = {"perspective", "orthographic"};
    static const std::vector<std::string> light_types = {"directional", "point", "spot"};
    for (const auto& camera : doc.cameras) {
        require_domain(doc, camera.line, "camera", "type", camera.type, true, camera_types);
        double value = 0;
        if (camera.type == "perspective") {
            if (!number_value(camera.values, "yfov", value))
                report(doc, camera.line, "a perspective camera requires <yfov>");
            else if (value <= 0 || value >= kPiOverTwo * 2)
                report(doc, camera.line, "yfov must be in (0, 2π) radians, got " + std::to_string(value));
            double znear = 0, zfar = 0;
            const bool has_near = number_value(camera.values, "znear", znear);
            const bool has_far = number_value(camera.values, "zfar", zfar);
            if (!has_near) report(doc, camera.line, "a perspective camera requires <znear>");
            if (!has_far) report(doc, camera.line, "a perspective camera requires <zfar>");
            if (has_near && znear <= 0) report(doc, camera.line, "znear must be > 0");
            if (has_near && has_far && zfar <= znear) report(doc, camera.line, "zfar must exceed znear");
            if (number_value(camera.values, "aspectRatio", value) && value <= 0)
                report(doc, camera.line, "aspectRatio must be > 0, or absent to derive it from the viewport");
            if (find_value(camera.values, "xmag") || find_value(camera.values, "ymag"))
                report(doc, camera.line, "orthographic magnification must not appear on a perspective camera");
        } else if (camera.type == "orthographic") {
            double xmag = 0, ymag = 0;
            if (!number_value(camera.values, "xmag", xmag)) report(doc, camera.line, "an orthographic camera requires <xmag>");
            if (!number_value(camera.values, "ymag", ymag)) report(doc, camera.line, "an orthographic camera requires <ymag>");
            if (xmag <= 0 || ymag <= 0) report(doc, camera.line, "xmag and ymag must be > 0");
            if (find_value(camera.values, "yfov")) report(doc, camera.line, "yfov belongs to a perspective camera");
        }
    }

    for (const auto& light : doc.lights) {
        require_domain(doc, light.line, "light", "type", light.type, true, light_types);
        double value = 0;
        for (const char* component : {"color.r", "color.g", "color.b"}) {
            const auto* found = find_value(light.values, component);
            if (!found) continue;
            if (!found->has_number)
                report(doc, found->line, std::string("light ") + component + " is not a number: " + found->text);
            else if (found->value < 0 || found->value > 1)
                report(doc, found->line, std::string("light ") + component + " must be in [0, 1]");
        }
        if (number_value(light.values, "intensity", value) && value < 0)
            report(doc, light.line, "light intensity must be >= 0");
        const bool has_range = number_value(light.values, "range", value);
        if (has_range && value < 0) report(doc, light.line, "light range must be >= 0, or absent for unbounded");
        if (light.type == "directional" && has_range)
            report(doc, light.line, "a directional light has no range (FORMAT.md §13)");
        double inner = 0, outer = 0;
        const bool has_inner = number_value(light.values, "innerConeAngle", inner);
        const bool has_outer = number_value(light.values, "outerConeAngle", outer);
        if (light.type == "spot") {
            if (!has_inner) report(doc, light.line, "a spot light requires <innerConeAngle>");
            if (!has_outer) report(doc, light.line, "a spot light requires <outerConeAngle>");
            if (has_inner && has_outer && inner > outer)
                report(doc, light.line, "innerConeAngle must not exceed outerConeAngle");
        } else if (has_inner || has_outer) {
            report(doc, light.line, "cone angles belong to a spot light, not a " + light.type + " one");
        }
        if (has_outer && outer > kPiOverTwo) report(doc, light.line, "outerConeAngle must be <= π/2");
    }
}

void validate_animations(Document& doc) {
    static const std::vector<std::string> properties = {"translation", "rotation", "scale", "morphWeights"};
    static const std::vector<std::string> interpolations = {"step", "linear"};
    for (const auto& animation : doc.animations) {
        const std::string owner = "animation id=\"" + animation.id + "\"";
        if (!animation.has_duration) report(doc, animation.line, "<" + owner + "> requires duration");
        else if (animation.duration <= 0) report(doc, animation.line, "duration must be > 0 seconds");
        if (animation.samplers.empty() && animation.channels.empty())
            report(doc, animation.line, "<" + owner + "> declares nothing", Diagnostic::Severity::warning);

        for (const auto& sampler : animation.samplers) {
            const std::string of = "sampler id=\"" + sampler.id + "\"";
            if (!sampler.has_input) report(doc, sampler.line, "<" + of + "> requires <input>");
            if (!sampler.has_output) report(doc, sampler.line, "<" + of + "> requires <output>");
            if (sampler.has_input && sampler.has_output && sampler.input.count >= 0 &&
                sampler.output.count >= 0 && sampler.input.count != sampler.output.count)
                report(doc, sampler.line, "<" + of + "> output count " + std::to_string(sampler.output.count) +
                                         " does not match input count " + std::to_string(sampler.input.count));
            for (const auto* stream : {&sampler.input, &sampler.output}) {
                if (stream->count < 0) {
                    report(doc, stream->line, "<" + stream->name + "> of <" + of + "> requires count");
                    continue;
                }
                if (stream->count < 1) report(doc, stream->line, "<" + stream->name + "> count must be at least 1");
                check_stream_mode(doc, *stream, of);
                check_stream_storage(doc, *stream);
            }
        }

        std::set<std::pair<std::string, std::string>> bound;
        for (const auto& channel : animation.channels) {
            if (channel.target.empty()) report(doc, channel.line, "<channel> requires target");
            if (channel.property.empty()) report(doc, channel.line, "<channel> requires property");
            if (channel.sampler.empty()) report(doc, channel.line, "<channel> requires sampler");
            require_domain(doc, channel.line, "channel", "property", channel.property, !channel.property.empty(), properties);
            require_domain(doc, channel.line, "channel", "interpolation", channel.interpolation, true, interpolations);
            if (!channel.target.empty() && !channel.property.empty() &&
                !bound.insert({channel.target, channel.property}).second)
                report(doc, channel.line, "<" + owner + "> animates '" + channel.property + "' on '" +
                                          channel.target + "' twice; one channel per pair");
            if (channel.property != "morphWeights" || channel.target.empty()) continue;
            const auto found = std::find_if(doc.primitives.begin(), doc.primitives.end(),
                                            [&](const Primitive& p) { return p.id == channel.target; });
            if (found == doc.primitives.end())
                report(doc, channel.line, "morphWeights channel targets '" + channel.target + "' which is not a primitive");
            else if (const auto* geometry = find_geometry(doc, found->geometry); geometry && !geometry->has_morph)
                report(doc, channel.line, "morphWeights channel targets primitive '" + channel.target +
                                          "' whose geometry '" + found->geometry + "' has no <morph>");
        }
    }
}

void validate_collisions(Document& doc) {
    static const std::vector<std::string> body_types = {"static", "dynamic", "kinematic"};
    static const std::vector<std::string> shape_types = {"box", "sphere", "capsule", "cylinder", "mesh"};
    static const std::vector<std::string> mesh_kinds = {"convex", "staticTriangleMesh"};
    for (const auto& body : doc.bodies) {
        require_domain(doc, body.line, "body", "type", body.type, true, body_types);
        if (body.node.empty()) report(doc, body.line, "<body id=\"" + body.id + "\"> requires node");
        if (body.shapes.empty()) report(doc, body.line, "<body id=\"" + body.id + "\"> declares no <shape>");
        for (const auto& shape : body.shapes) {
            require_domain(doc, shape.line, "shape", "type", shape.type, true, shape_types);
            auto positive = [&](const char* key) {
                const auto* value = find_value(shape.values, key);
                if (!value) {
                    report(doc, shape.line, std::string("<shape type=\"") + shape.type + "\"> requires " + key);
                    return;
                }
                if (!value->has_number)
                    report(doc, value->line, std::string(key) + " is not a number: " + value->text);
                else if (value->value <= 0)
                    report(doc, value->line, std::string(key) + " must be > 0");
            };
            if (shape.type == "sphere") positive("radius");
            if (shape.type == "capsule" || shape.type == "cylinder") { positive("radius"); positive("height"); }
            if (shape.type == "box") {
                const auto* value = find_value(shape.values, "extent");
                std::vector<double> extent;
                if (!value) report(doc, shape.line, "<shape type=\"box\"> requires extent");
                else if (!split_numbers(value->text, extent) || extent.size() != 3 ||
                         std::any_of(extent.begin(), extent.end(), [](double v) { return v <= 0; }))
                    report(doc, value->line, "<shape type=\"box\"> extent must be three positive numbers: " + value->text);
            }
            if (shape.type == "mesh") {
                if (shape.geometry.empty()) report(doc, shape.line, "<shape type=\"mesh\"> requires geometry");
                require_domain(doc, shape.line, "shape", "kind", shape.kind, true, mesh_kinds);
                if (shape.kind == "staticTriangleMesh" && body.type != "static")
                    report(doc, shape.line, "a staticTriangleMesh shape requires a static body (FORMAT.md §15)");
            }
            if (find_value(shape.values, "scale"))
                report(doc, shape.line, "shape-local scale is unsupported in v1.0 (FORMAT.md §15)");
        }
    }
}

void validate_scene(Document& doc) {
    static const std::vector<std::string> transform_keys = {"translation", "rotation", "scale"};
    std::size_t roots = 0;
    for (const auto& node : doc.nodes) {
        const std::string owner = "node id=\"" + node.id + "\"";
        if (node.depth == 3) ++roots;
        if (node.camera_refs.size() > 1)
            report(doc, node.line, "<" + owner + "> carries " + std::to_string(node.camera_refs.size()) +
                                   " cameras; at most one is allowed (FORMAT.md §16)");
        for (const auto& value : node.values) {
            const auto key = std::find(transform_keys.begin(), transform_keys.end(), value.key);
            if (key == transform_keys.end()) continue;
            std::vector<double> parts;
            const auto expected = value.key == "rotation" ? 4 : 3;
            if (!split_numbers(value.text, parts) || static_cast<long long>(parts.size()) != expected) {
                report(doc, value.line, "<" + owner + "> " + value.key + " must be " + std::to_string(expected) +
                                        " whitespace-separated numbers: " + value.text);
                continue;
            }
            if (value.key == "rotation") {
                double squared = 0;
                for (const auto part : parts) squared += part * part;
                if (std::abs(std::sqrt(squared) - 1.0) > kQuaternionTolerance)
                    report(doc, value.line, "<" + owner + "> rotation quaternion must be normalised to within 1e-4 "
                                            "(FORMAT.md §4)");
            }
            if (value.key == "scale")
                for (const auto part : parts)
                    if (part == 0) report(doc, value.line, "<" + owner + "> scale components must be non-zero (FORMAT.md §6)");
        }
    }
    if (static_cast<long long>(doc.nodes.size()) > kMaxNodes)
        report(doc, 1, "the scene has more than " + std::to_string(kMaxNodes) + " nodes (FORMAT.md §17)");
    if (!doc.nodes.empty() && roots != 1)
        report(doc, doc.nodes.front().line, "<scene> must contain exactly one root <node> (found " +
                                            std::to_string(roots) + ")");
}

} // namespace

void validate(Document& doc) {
    validate_ids(doc);
    validate_textures(doc);
    validate_geometry(doc);
    validate_pool(doc);
    validate_skeleton(doc);
    validate_materials(doc);
    validate_cameras_lights(doc);
    validate_animations(doc);
    validate_collisions(doc);
    validate_scene(doc);
}

} // namespace g3d
