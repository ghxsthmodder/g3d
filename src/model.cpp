#include "g3d/model.hpp"

#include "internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace g3d {
namespace {

constexpr std::size_t kMaxDocumentBytes = 8ull * 1024 * 1024;
constexpr std::size_t kMaxDepth = 64;

struct Tag {
    std::string name;
    std::unordered_map<std::string, std::string> attrs;
    bool closing{false};
    bool self_closing{false};
    std::size_t line{1};
};

struct Frame {
    std::string name;
    std::size_t line{1};
    std::size_t node_index{SIZE_MAX}; // only set for a <node> frame
    long long text_numbers{0};        // values seen in this element's text, for streams
};

struct Reference {
    std::string id;
    std::string kind;
    std::size_t line{1};
};

bool name_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || c == ':' || c == '.';
}

bool parse_tag(std::string_view raw, std::size_t line, Tag& out, std::string& error) {
    out.line = line;
    std::size_t i = 0;
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    if (i < raw.size() && raw[i] == '/') { out.closing = true; ++i; }
    while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
    const auto begin = i;
    while (i < raw.size() && name_char(raw[i])) ++i;
    if (begin == i) { error = "tag name is missing"; return false; }
    out.name = std::string(raw.substr(begin, i - begin));
    if (out.closing) {
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i != raw.size()) { error = "unexpected content in closing tag"; return false; }
        return true;
    }

    while (i < raw.size()) {
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i == raw.size()) break;
        if (raw[i] == '/') {
            ++i;
            while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
            if (i != raw.size()) { error = "unexpected content after '/'"; return false; }
            out.self_closing = true;
            break;
        }
        const auto key_begin = i;
        while (i < raw.size() && name_char(raw[i])) ++i;
        if (key_begin == i) { error = "invalid attribute name"; return false; }
        std::string key(raw.substr(key_begin, i - key_begin));
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i == raw.size() || raw[i++] != '=') { error = "expected '=' after attribute name"; return false; }
        while (i < raw.size() && std::isspace(static_cast<unsigned char>(raw[i]))) ++i;
        if (i == raw.size() || (raw[i] != '"' && raw[i] != '\'')) {
            error = "attribute value must be quoted"; return false;
        }
        const char quote = raw[i++];
        const auto value_begin = i;
        while (i < raw.size() && raw[i] != quote) {
            if (raw[i] == '<') { error = "'<' is not allowed in an attribute value"; return false; }
            ++i;
        }
        if (i == raw.size()) { error = "unterminated attribute value"; return false; }
        if (!out.attrs.emplace(std::move(key), std::string(raw.substr(value_begin, i - value_begin))).second) {
            error = "duplicate attribute"; return false;
        }
        ++i;
    }
    return true;
}

/// The '>' that closes a tag: the first one outside a quoted attribute value. `>` is legal
/// inside a value — only `<` and `&` must be escaped — and the §8.4 `when` DSL writes
/// `apiLevel >= 26`, so scanning for a bare '>' would end the tag mid-value and the rest of
/// the tag would surface as element text.
std::size_t tag_end(std::string_view xml, std::size_t open) {
    for (std::size_t i = open + 1; i < xml.size(); ++i) {
        const char c = xml[i];
        if (c == '"' || c == '\'') {
            const auto close = xml.find(c, i + 1);
            if (close == std::string_view::npos) return std::string_view::npos;
            i = close;
        } else if (c == '>') return i;
    }
    return std::string_view::npos;
}

std::string decode_entities(std::string_view s, bool& ok) {
    std::string result;
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') { result += s[i++]; continue; }
        const auto end = s.find(';', i + 1);
        if (end == std::string_view::npos) { ok = false; return {}; }
        const auto entity = s.substr(i + 1, end - i - 1);
        if (entity == "amp") result += '&';
        else if (entity == "lt") result += '<';
        else if (entity == "gt") result += '>';
        else if (entity == "quot") result += '"';
        else if (entity == "apos") result += '\'';
        else { ok = false; return {}; }
        i = end + 1;
    }
    return result;
}

const std::string* attr(const Tag& tag, const char* name) {
    const auto found = tag.attrs.find(name);
    return found == tag.attrs.end() ? nullptr : &found->second;
}

std::string str_attr(const Tag& tag, const char* name) {
    const auto* value = attr(tag, name);
    return value ? *value : std::string();
}

long long int_attr(Document& doc, const Tag& tag, const char* name, long long fallback) {
    const auto* value = attr(tag, name);
    if (!value) return fallback;
    double parsed = 0;
    if (!number_text(*value, parsed) || parsed != std::floor(parsed) ||
        parsed > static_cast<double>(LLONG_MAX) || parsed < -static_cast<double>(LLONG_MAX)) {
        report(doc, tag.line, "<" + tag.name + "> attribute '" + name + "' is not an integer: " + *value);
        return fallback;
    }
    return static_cast<long long>(parsed);
}

bool bool_attr(Document& doc, const Tag& tag, const char* name, bool fallback) {
    const auto* value = attr(tag, name);
    if (!value) return fallback;
    if (*value == "true" || *value == "1") return true;
    if (*value == "false" || *value == "0") return false;
    report(doc, tag.line, "<" + tag.name + "> attribute '" + name + "' is not true, false, 1 or 0: " + *value);
    return fallback;
}

std::vector<std::string> split_list(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        const auto begin = i;
        while (i < text.size() && !std::isspace(static_cast<unsigned char>(text[i]))) ++i;
        if (begin != i) out.emplace_back(text.substr(begin, i - begin));
    }
    return out;
}

/// Records an element's numeric attributes under a lookup key so a rule can find the
/// value later without re-reading the XML: `value` becomes the element itself (`yfov`),
/// any other attribute is qualified by it (`color.r`). Structural attributes are skipped
/// because the model already carries them.
void collect_values(const std::string& prefix, const Tag& tag, std::vector<NamedValue>& out) {
    static const std::array<const char*, 16> skipped = {
        "id", "name", "type", "geometry", "kind", "src", "view", "count", "componentType",
        "normalized", "byteOffset", "byteLength", "byteStride", "target", "assetId", "alphaMode"};
    for (const auto& [key, text] : tag.attrs) {
        if (std::find(skipped.begin(), skipped.end(), std::string(key)) != skipped.end()) continue;
        NamedValue entry;
        entry.key = key == "value" ? prefix : (prefix.empty() ? key : prefix + "." + key);
        entry.text = text;
        entry.has_number = number_text(text, entry.value);
        entry.line = tag.line;
        out.push_back(std::move(entry));
    }
}

Stream make_stream(Document& doc, const Tag& tag) {
    Stream stream;
    stream.name = tag.name;
    stream.line = tag.line;
    stream.view = str_attr(tag, "view");
    stream.component_type = str_attr(tag, "componentType");
    stream.topology = str_attr(tag, "topology");
    stream.count = int_attr(doc, tag, "count", -1);
    stream.byte_offset = int_attr(doc, tag, "byteOffset", 0);
    stream.normalized = bool_attr(doc, tag, "normalized", false);
    return stream;
}

bool contains_name(const std::vector<Stream>& streams, const std::string& name) {
    return std::any_of(streams.begin(), streams.end(),
                       [&](const Stream& s) { return s.name == name; });
}

/// Elements whose text node is the stream's data in ASCII authoring mode. Their token
/// count is collected so BINARY.md §7 can tell "empty" from "written", and FORMAT.md §10
/// can compare it against `count × components`.
bool is_stream_element(const std::string& name) {
    static const std::array<const char*, 14> names = {"positions", "normals", "tangents", "uv0", "uv1",
                                                     "colors0", "joints0", "weights0", "indices",
                                                     "targetPositions", "targetNormals", "input", "output",
                                                     "inverseBindMatrices"};
    return std::find(names.begin(), names.end(), name) != names.end();
}

} // namespace

void report(Document& doc, std::size_t line, std::string message, Diagnostic::Severity severity) {
    doc.diagnostics.push_back({severity, line, std::move(message)});
}

bool number_text(std::string_view text, double& out) {
    if (text.empty()) return false;
    // FORMAT.md §6: dot decimal separator, scientific notation allowed, and nothing else.
    // strtod would accept a '+' prefix, hex floats and the spellings inf/nan, so the
    // spelling is checked before the value is.
    if (text.front() == '+' || text.find('x') != std::string_view::npos ||
        text.find('X') != std::string_view::npos)
        return false;
    const std::string scratch(text);
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtod(scratch.c_str(), &end);
    if (errno == ERANGE || end == scratch.c_str() || *end != '\0') return false;
    if (!std::isfinite(value)) return false;
    out = value;
    return true;
}

bool Document::valid() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.severity == Diagnostic::Severity::error;
    });
}

Document parse(std::string_view xml, std::filesystem::path source_path) {
    Document doc;
    doc.source_path = std::move(source_path);
    if (xml.size() > kMaxDocumentBytes)
        report(doc, 1, "document exceeds the 8 MiB limit (FORMAT.md §17)");

    std::vector<Frame> stack;
    std::set<std::string> asset_ids, geometry_ids, primitive_ids, material_ids, node_ids, buffer_ids,
        view_ids, skeleton_ids, skin_ids, camera_ids, light_ids, animation_ids, sampler_ids, body_ids;
    std::vector<Reference> references;
    std::set<std::string> sections;
    const std::vector<std::string> section_order = {"metadata","assets","skeletons","mesh","materials",
                                                    "cameras","lights","animations","collisions","scene"};
    static const std::array<const char*, 9> stream_names = {"positions","normals","tangents","uv0","uv1",
                                                            "colors0","joints0","weights0","indices"};
    static const std::array<const char*, 7> slot_names = {"baseColor","metallicRoughness","metallic",
                                                          "roughness","occlusion","normal","emissive"};
    int last_section = -1;
    bool root_seen = false, root_closed = false, scene_seen = false, node_pushed = false;
    std::size_t line = 1, i = 0, scene_line = 1;

    auto parent_name = [&]() -> std::string { return stack.empty() ? std::string() : stack.back().name; };
    auto current_node = [&]() -> Node* {
        for (auto it = stack.rbegin(); it != stack.rend(); ++it)
            if (it->name == "node" && it->node_index < doc.nodes.size()) return &doc.nodes[it->node_index];
        return nullptr;
    };
    auto add_view_reference = [&](const Stream& stream) {
        if (!stream.view.empty()) references.push_back({stream.view, "bufferView", stream.line});
    };
    /// The collected Stream a closing tag belongs to, found by name and opening line in
    /// whichever container is currently open. Text is counted while the element is on the
    /// stack, so this is how it reaches the validator.
    auto stream_slot = [&](const std::string& name, std::size_t opened) -> Stream* {
        auto pick = [](std::vector<Stream>& list, const std::string& n, std::size_t line) -> Stream* {
            for (auto& stream : list) if (stream.name == n && stream.line == line) return &stream;
            return nullptr;
        };
        if (!doc.geometries.empty()) {
            auto& geometry = doc.geometries.back();
            if (auto* stream = pick(geometry.streams, name, opened)) return stream;
            if (auto* stream = pick(geometry.morph_streams, name, opened)) return stream;
        }
        if (!doc.skeletons.empty()) {
            auto& skeleton = doc.skeletons.back();
            if (skeleton.has_inverse_bind && skeleton.inverse_bind.name == name &&
                skeleton.inverse_bind.line == opened)
                return &skeleton.inverse_bind;
        }
        if (!doc.animations.empty() && !doc.animations.back().samplers.empty()) {
            auto& sampler = doc.animations.back().samplers.back();
            if (sampler.has_input && sampler.input.name == name && sampler.input.line == opened)
                return &sampler.input;
            if (sampler.has_output && sampler.output.name == name && sampler.output.line == opened)
                return &sampler.output;
        }
        return nullptr;
    };

    while (i < xml.size()) {
        if (xml[i] == '\n') { ++line; ++i; continue; }
        if (xml[i] != '<') {
            const auto next = xml.find('<', i);
            const auto end = next == std::string_view::npos ? xml.size() : next;
            const auto text = xml.substr(i, end - i);
            if (text.find("]]>") != std::string_view::npos)
                report(doc, line, "']]>' is not allowed in ordinary text");
            bool entities_ok = true;
            const auto decoded = decode_entities(text, entities_ok);
            if (!entities_ok) report(doc, line, "unknown or malformed XML entity");
            if (entities_ok && !stack.empty() && is_stream_element(stack.back().name)) {
                const std::string what = "<" + stack.back().name + ">";
                const std::size_t opened = stack.back().line;
                for (const auto& token : split_list(decoded)) {
                    double unused = 0;
                    if (!number_text(token, unused))
                        report(doc, opened, what + " text value is not a finite number: " + token);
                    else
                        ++stack.back().text_numbers;
                }
            }
            for (char c : text) if (c == '\n') ++line;
            i = end;
            continue;
        }

        if (xml.substr(i, 4) == "<!--") {
            const auto end = xml.find("-->", i + 4);
            if (end == std::string_view::npos) { report(doc, line, "unterminated comment"); break; }
            if (xml.substr(i + 4, end - i - 4).find("--") != std::string_view::npos)
                report(doc, line, "double hyphen is not allowed inside comments");
            for (char c : xml.substr(i, end + 3 - i)) if (c == '\n') ++line;
            i = end + 3;
            continue;
        }
        if (xml.substr(i, 2) == "<?") {
            const auto end = xml.find("?>", i + 2);
            if (end == std::string_view::npos) { report(doc, line, "unterminated processing instruction"); break; }
            auto pi = xml.substr(i + 2, end - i - 2);
            if (!pi.starts_with("xml") || (pi.size() > 3 && !std::isspace(static_cast<unsigned char>(pi[3])))) {
                report(doc, line, "processing instructions are not supported in G3D v1.0");
                const auto v = pi.find("version=");
                if (v != std::string_view::npos) {
                    auto q = pi.find_first_of("\"'", v + 8);
                    if (q != std::string_view::npos) {
                        auto qe = pi.find(pi[q], q + 1);
                        if (qe != std::string_view::npos) doc.version = std::string(pi.substr(q + 1, qe - q - 1));
                    }
                }
            }
            for (char c : xml.substr(i, end + 2 - i)) if (c == '\n') ++line;
            i = end + 2;
            continue;
        }
        if (xml.substr(i, 2) == "<!") {
            report(doc, line, "DOCTYPE and declarations are not supported by the initial parser");
            const auto end = xml.find('>', i + 2);
            if (end == std::string_view::npos) break;
            i = end + 1;
            continue;
        }

        const auto end = tag_end(xml, i);
        if (end == std::string_view::npos) { report(doc, line, "unterminated tag"); break; }
        Tag tag;
        std::string error;
        if (!parse_tag(xml.substr(i + 1, end - i - 1), line, tag, error)) {
            report(doc, line, error);
            i = end + 1;
            continue;
        }

        if (tag.closing) {
            if (stack.empty()) report(doc, line, "closing tag without matching opener: " + tag.name);
            else {
                if (stack.back().name != tag.name)
                    report(doc, line, "mismatched closing tag: " + tag.name + " (expected " + stack.back().name + ")");
                if (stack.back().text_numbers > 0)
                    if (auto* stream = stream_slot(stack.back().name, stack.back().line))
                        stream->ascii_numbers += stack.back().text_numbers;
                stack.pop_back();
            }
            if (stack.empty() && root_seen) root_closed = true;
        } else {
            const bool is_root = !root_seen;
            const std::string parent = parent_name();

            if (!root_seen) {
                root_seen = true;
                if (tag.name != "g3d") report(doc, line, "root element must be <g3d>");
                auto v = tag.attrs.find("version");
                if (v == tag.attrs.end() || v->second.empty()) report(doc, line, "<g3d> requires version=\"1.0\"");
                else if (v->second != "1.0") report(doc, line, "unsupported G3D version: " + v->second);
                else doc.version = v->second;
                for (const auto* flag : {"extensionsUsed", "extensionsRequired"})
                    if (const auto* value = attr(tag, flag); value && value->empty())
                        report(doc, line, std::string("'<g3d> attribute '") + flag +
                                    "' is declared but lists no prefix");
            } else if (root_closed || stack.empty()) {
                report(doc, line, "multiple root elements");
            }

            if (!is_root && stack.size() == 1) {
                auto section = std::find(section_order.begin(), section_order.end(), tag.name);
                if (section == section_order.end()) report(doc, line, "unknown top-level section: " + tag.name);
                else {
                    const int index = static_cast<int>(section - section_order.begin());
                    if (!sections.insert(tag.name).second) report(doc, line, "duplicate top-level section: " + tag.name);
                    if (index < last_section) report(doc, line, "top-level sections are out of order");
                    last_section = std::max(last_section, index);
                    if (tag.name == "scene") { scene_seen = true; scene_line = line; }
                }
            }

            // ---- collection ------------------------------------------------------
            if (tag.name == "asset") {
                const auto id = tag.attrs.find("id");
                const auto type = tag.attrs.find("type");
                const auto src = tag.attrs.find("src");
                if (id == tag.attrs.end() || id->second.empty()) report(doc, line, "<asset> requires a non-empty id");
                else if (!asset_ids.insert(id->second).second) report(doc, line, "duplicate asset id: " + id->second);
                if (type == tag.attrs.end() || type->second != "texture")
                    report(doc, line, "<asset> type must be texture in v1.0");
                if (src == tag.attrs.end() || src->second.empty()) report(doc, line, "<asset> requires src");
                for (const auto* duplicated : {"width", "height", "depth", "format", "srgb", "layers", "levelCount"})
                    if (attr(tag, duplicated))
                        report(doc, line, std::string("<asset> must not state '") + duplicated +
                                    "'; the KTX2 header is authoritative (FORMAT.md §8.2)");
                Asset asset;
                if (id != tag.attrs.end()) asset.id = id->second;
                if (type != tag.attrs.end()) asset.type = type->second;
                if (src != tag.attrs.end()) asset.file = src->second;
                if (const auto* kind = attr(tag, "kind")) asset.kind = *kind;
                if (const auto* rp = attr(tag, "relativePath"))
                    asset.relative_path = *rp != "false" && *rp != "0";
                asset.line = line;
                doc.assets.push_back(std::move(asset));
            }

            if (tag.name == "textureSet") {
                // A set shares the asset ID category: a material slot addresses the set,
                // an <image> addresses one codec variant of it.
                const auto id = tag.attrs.find("id");
                if (id == tag.attrs.end() || id->second.empty()) report(doc, line, "<textureSet> requires a non-empty id");
                else if (!asset_ids.insert(id->second).second) report(doc, line, "duplicate asset id: " + id->second);
                TextureSet set;
                if (id != tag.attrs.end()) set.id = id->second;
                set.line = line;
                doc.texture_sets.push_back(std::move(set));
            }
            if (tag.name == "image" && parent == "textureSet" && !doc.texture_sets.empty()) {
                TextureVariant image;
                image.asset_id = str_attr(tag, "assetId");
                image.vk_format = str_attr(tag, "vkFormat");
                image.when = str_attr(tag, "when");
                image.prefer = str_attr(tag, "prefer");
                if (attr(tag, "default")) {
                    image.has_default = true;
                    image.is_default = bool_attr(doc, tag, "default", false);
                }
                image.line = line;
                doc.texture_sets.back().images.push_back(std::move(image));
            }

            const auto section_src = std::array<const char*, 10>{"mesh","animations","collisions","materials",
                                                                "assets","skeletons","cameras","lights","pool","license"};
            if (std::find(section_src.begin(), section_src.end(), std::string(tag.name)) != section_src.end())
                if (const auto* src = attr(tag, "src")) doc.external_files.push_back(*src);

            if (tag.name == "buffer" && parent == "pool") {
                Buffer buffer;
                buffer.id = str_attr(tag, "id");
                buffer.byte_offset = int_attr(doc, tag, "byteOffset", -1);
                buffer.byte_length = int_attr(doc, tag, "byteLength", -1);
                buffer.align = int_attr(doc, tag, "align", 1); // BINARY.md §4: absent means no extra requirement
                buffer.line = line;
                if (buffer.id.empty()) report(doc, line, "<buffer> requires id");
                else if (!buffer_ids.insert(buffer.id).second) report(doc, line, "duplicate buffer id: " + buffer.id);
                doc.buffers.push_back(std::move(buffer));
            }
            if (tag.name == "bufferView") {
                BufferView view;
                view.id = str_attr(tag, "id");
                view.buffer = str_attr(tag, "buffer");
                view.byte_offset = int_attr(doc, tag, "byteOffset", -1);
                view.byte_length = int_attr(doc, tag, "byteLength", -1);
                view.byte_stride = int_attr(doc, tag, "byteStride", 0);
                if (const auto* target = attr(tag, "target")) view.target = *target;
                view.line = line;
                if (view.id.empty()) report(doc, line, "<bufferView> requires id");
                else if (!view_ids.insert(view.id).second) report(doc, line, "duplicate bufferView id: " + view.id);
                if (!view.buffer.empty()) references.push_back({view.buffer, "buffer", line});
                doc.views.push_back(std::move(view));
            }

            if (tag.name == "geometry" && parent == "mesh") {
                Geometry geometry;
                geometry.id = str_attr(tag, "id");
                geometry.line = line;
                if (geometry.id.empty()) report(doc, line, "<geometry> requires id");
                else if (!geometry_ids.insert(geometry.id).second) report(doc, line, "duplicate geometry id: " + geometry.id);
                doc.geometries.push_back(std::move(geometry));
            }
            if (parent == "geometry" && std::find(stream_names.begin(), stream_names.end(), std::string(tag.name))
                                          != stream_names.end()) {
                if (doc.geometries.empty()) report(doc, line, "<" + tag.name + "> appears outside <geometry>");
                else {
                    auto& geometry = doc.geometries.back();
                    if (contains_name(geometry.streams, tag.name))
                        report(doc, line, "duplicate <" + tag.name + "> stream in <geometry id=\"" + geometry.id + "\">");
                    auto stream = make_stream(doc, tag);
                    add_view_reference(stream);
                    geometry.streams.push_back(std::move(stream));
                }
            }
            if (tag.name == "quantization") {
                Stream* positions = nullptr;
                if (parent == "positions" && !doc.geometries.empty())
                    for (auto& stream : doc.geometries.back().streams)
                        if (stream.name == "positions") positions = &stream;
                if (!positions) report(doc, line, "<quantization> is only valid inside <positions>");
                else {
                    if (positions->has_quantization)
                        report(doc, line, "<positions> already declares <quantization>");
                    positions->has_quantization = true;
                    collect_values("", tag, positions->quantization);
                }
            }
            if (tag.name == "morph" && parent == "geometry" && !doc.geometries.empty()) {
                auto& geometry = doc.geometries.back();
                if (geometry.has_morph) report(doc, line, "<geometry> already declares a <morph>");
                geometry.has_morph = true;
                geometry.morph_line = line;
                geometry.morph_count = int_attr(doc, tag, "count", -1);
            }
            if (parent == "morph" && !doc.geometries.empty()) {
                auto& geometry = doc.geometries.back();
                if (tag.name == "target") {
                    const auto id = str_attr(tag, "id");
                    if (id.empty()) report(doc, line, "<target> requires id");
                    else if (std::find(geometry.morph_targets.begin(), geometry.morph_targets.end(), id) !=
                             geometry.morph_targets.end())
                        report(doc, line, "duplicate morph target id: " + id);
                    else geometry.morph_targets.push_back(id);
                }
                if (tag.name == "targetPositions" || tag.name == "targetNormals") {
                    if (contains_name(geometry.morph_streams, tag.name))
                        report(doc, line, "duplicate <" + tag.name + "> in <morph>");
                    auto stream = make_stream(doc, tag);
                    add_view_reference(stream);
                    geometry.morph_streams.push_back(std::move(stream));
                }
            }
            if (tag.name == "primitive") {
                Primitive primitive;
                primitive.id = str_attr(tag, "id");
                primitive.geometry = str_attr(tag, "geometry");
                primitive.material = str_attr(tag, "material");
                primitive.skin = str_attr(tag, "skin");
                primitive.morph_weight_count = static_cast<long long>(split_list(str_attr(tag, "morphWeights")).size());
                primitive.line = line;
                if (primitive.id.empty()) report(doc, line, "<primitive> requires id");
                else if (!primitive_ids.insert(primitive.id).second) report(doc, line, "duplicate primitive id: " + primitive.id);
                if (primitive.geometry.empty()) report(doc, line, "<primitive> requires geometry");
                else references.push_back({primitive.geometry, "geometry", line});
                if (primitive.material.empty()) report(doc, line, "<primitive> requires material");
                else references.push_back({primitive.material, "material", line});
                if (!primitive.skin.empty()) references.push_back({primitive.skin, "skin", line});
                doc.primitives.push_back(std::move(primitive));
            }

            if (tag.name == "skeleton" && parent == "skeletons") {
                Skeleton skeleton;
                skeleton.id = str_attr(tag, "id");
                skeleton.line = line;
                if (skeleton.id.empty()) report(doc, line, "<skeleton> requires id");
                else if (!skeleton_ids.insert(skeleton.id).second) report(doc, line, "duplicate skeleton id: " + skeleton.id);
                for (auto& joint : split_list(str_attr(tag, "nodes"))) {
                    references.push_back({joint, "node", line});
                    skeleton.nodes.push_back(std::move(joint));
                }
                doc.skeletons.push_back(std::move(skeleton));
            }
            if (tag.name == "joint" && parent == "skeleton" && !doc.skeletons.empty()) {
                Joint joint;
                joint.id = str_attr(tag, "id");
                joint.line = line;
                if (const auto* value = attr(tag, "parent")) { joint.parent = *value; joint.has_parent = true; }
                if (joint.id.empty()) report(doc, line, "<joint> requires id");
                doc.skeletons.back().joints.push_back(std::move(joint));
            }
            if (tag.name == "inverseBindMatrices" && parent == "skeleton" && !doc.skeletons.empty()) {
                auto& skeleton = doc.skeletons.back();
                if (skeleton.has_inverse_bind) report(doc, line, "<skeleton> already declares <inverseBindMatrices>");
                skeleton.has_inverse_bind = true;
                skeleton.inverse_bind = make_stream(doc, tag);
                skeleton.inverse_bind.name = "inverseBindMatrices";
                add_view_reference(skeleton.inverse_bind);
            }
            if (tag.name == "skin" && parent == "skeletons") {
                Skin skin;
                skin.id = str_attr(tag, "id");
                skin.skeleton = str_attr(tag, "skeleton");
                skin.max_influences = str_attr(tag, "maxInfluences");
                skin.line = line;
                if (skin.id.empty()) report(doc, line, "<skin> requires id");
                else if (!skin_ids.insert(skin.id).second) report(doc, line, "duplicate skin id: " + skin.id);
                if (!skin.skeleton.empty()) references.push_back({skin.skeleton, "skeleton", line});
                doc.skins.push_back(std::move(skin));
            }
            if (tag.name == "bind" && parent == "skin" && !doc.skins.empty()) {
                Bind bind;
                bind.node = str_attr(tag, "node");
                if (const auto* value = attr(tag, "mode")) bind.mode = *value;
                bind.line = line;
                if (!bind.node.empty()) references.push_back({bind.node, "node", line});
                doc.skins.back().binds.push_back(std::move(bind));
            }

            if (tag.name == "material" && parent == "materials") {
                Material material;
                material.id = str_attr(tag, "id");
                material.name = str_attr(tag, "name");
                if (const auto* value = attr(tag, "alphaMode")) material.alpha_mode = *value;
                material.has_alpha_cutoff = attr(tag, "alphaCutoff") != nullptr;
                bool_attr(doc, tag, "premultiplied", false);
                bool_attr(doc, tag, "doubleSided", false);
                collect_values("", tag, material.values);
                material.line = line;
                if (material.id.empty()) report(doc, line, "<material> requires id");
                else if (!material_ids.insert(material.id).second) report(doc, line, "duplicate material id: " + material.id);
                doc.materials.push_back(std::move(material));
            }
            if (!doc.materials.empty() && (parent == "material" || parent == "pbr") &&
                std::find(slot_names.begin(), slot_names.end(), std::string(tag.name)) != slot_names.end())
                collect_values(tag.name, tag, doc.materials.back().values);
            if (tag.name == "pbr" && parent == "material" && !doc.materials.empty())
                if (const auto* value = attr(tag, "workflow")) doc.materials.back().workflow = *value;
            if (tag.name == "texture" && !doc.materials.empty() &&
                std::find(slot_names.begin(), slot_names.end(), std::string(parent)) != slot_names.end()) {
                TextureSlot slot;
                slot.asset_id = str_attr(tag, "assetId");
                slot.colorspace = str_attr(tag, "colorspace");
                slot.slot = parent;
                slot.tex_coord = int_attr(doc, tag, "texCoord", -1);
                slot.layer = attr(tag, "layer") ? int_attr(doc, tag, "layer", -1) : -1;
                slot.line = line;
                doc.materials.back().slots.push_back(std::move(slot));
            }

            if (tag.name == "camera") {
                if (parent == "cameras") {
                    Camera camera;
                    camera.id = str_attr(tag, "id");
                    camera.type = str_attr(tag, "type");
                    camera.line = line;
                    if (camera.id.empty()) report(doc, line, "<camera> requires id");
                    else if (!camera_ids.insert(camera.id).second) report(doc, line, "duplicate camera id: " + camera.id);
                    doc.cameras.push_back(std::move(camera));
                } else if (parent == "node") {
                    const auto ref = str_attr(tag, "ref");
                    if (ref.empty()) report(doc, line, "<node><camera> requires ref");
                    else if (auto* node = current_node()) {
                        node->camera_refs.push_back(ref);
                        references.push_back({ref, "camera", line});
                    }
                }
            } else if (tag.name == "light") {
                if (parent == "lights") {
                    Light light;
                    light.id = str_attr(tag, "id");
                    light.type = str_attr(tag, "type");
                    light.line = line;
                    if (light.id.empty()) report(doc, line, "<light> requires id");
                    else if (!light_ids.insert(light.id).second) report(doc, line, "duplicate light id: " + light.id);
                    doc.lights.push_back(std::move(light));
                } else if (parent == "node") {
                    const auto ref = str_attr(tag, "ref");
                    if (ref.empty()) report(doc, line, "<node><light> requires ref");
                    else if (auto* node = current_node()) {
                        node->light_refs.push_back(ref);
                        references.push_back({ref, "light", line});
                    }
                }
            }
            if (parent == "camera" && !doc.cameras.empty()) collect_values(tag.name, tag, doc.cameras.back().values);
            if (parent == "light" && !doc.lights.empty()) collect_values(tag.name, tag, doc.lights.back().values);

            if (tag.name == "animation" && parent == "animations") {
                Animation animation;
                animation.id = str_attr(tag, "id");
                animation.line = line;
                if (const auto* value = attr(tag, "duration")) {
                    animation.has_duration = true;
                    if (!number_text(*value, animation.duration))
                        report(doc, line, "<animation> attribute 'duration' is not a number: " + *value);
                }
                if (animation.id.empty()) report(doc, line, "<animation> requires id");
                else if (!animation_ids.insert(animation.id).second) report(doc, line, "duplicate animation id: " + animation.id);
                doc.animations.push_back(std::move(animation));
            }
            if (tag.name == "sampler" && parent == "animation" && !doc.animations.empty()) {
                Sampler sampler;
                sampler.id = str_attr(tag, "id");
                sampler.line = line;
                if (sampler.id.empty()) report(doc, line, "<sampler> requires id");
                else if (!sampler_ids.insert(sampler.id).second) report(doc, line, "duplicate sampler id: " + sampler.id);
                doc.animations.back().samplers.push_back(std::move(sampler));
            }
            if ((tag.name == "input" || tag.name == "output") && parent == "sampler" &&
                !doc.animations.empty() && !doc.animations.back().samplers.empty()) {
                auto& sampler = doc.animations.back().samplers.back();
                auto stream = make_stream(doc, tag);
                add_view_reference(stream);
                if (tag.name == "input") {
                    if (sampler.has_input) report(doc, line, "<sampler> already declares <input>");
                    sampler.has_input = true;
                    sampler.input = std::move(stream);
                } else {
                    if (sampler.has_output) report(doc, line, "<sampler> already declares <output>");
                    sampler.has_output = true;
                    sampler.output = std::move(stream);
                }
            }
            if (tag.name == "channel" && parent == "animation" && !doc.animations.empty()) {
                Channel channel;
                channel.target = str_attr(tag, "target");
                channel.property = str_attr(tag, "property");
                channel.sampler = str_attr(tag, "sampler");
                if (const auto* value = attr(tag, "interpolation")) channel.interpolation = *value;
                channel.line = line;
                if (!channel.sampler.empty()) references.push_back({channel.sampler, "sampler", line});
                doc.animations.back().channels.push_back(std::move(channel));
            }

            if (tag.name == "body" && parent == "collisions") {
                Body body;
                body.id = str_attr(tag, "id");
                body.node = str_attr(tag, "node");
                body.type = str_attr(tag, "type");
                body.line = line;
                if (body.id.empty()) report(doc, line, "<body> requires id");
                else if (!body_ids.insert(body.id).second) report(doc, line, "duplicate body id: " + body.id);
                if (!body.node.empty()) references.push_back({body.node, "node", line});
                doc.bodies.push_back(std::move(body));
            }
            if (tag.name == "shape" && parent == "body" && !doc.bodies.empty()) {
                Shape shape;
                shape.type = str_attr(tag, "type");
                shape.geometry = str_attr(tag, "geometry");
                shape.kind = str_attr(tag, "kind");
                shape.line = line;
                collect_values("", tag, shape.values);
                if (!shape.geometry.empty()) references.push_back({shape.geometry, "geometry", line});
                doc.bodies.back().shapes.push_back(std::move(shape));
            }

            if (tag.name == "node" && (parent == "scene" || parent == "node")) {
                Node node;
                node.id = str_attr(tag, "id");
                node.depth = stack.size() + 1;
                node.line = line;
                collect_values("", tag, node.values);
                if (node.id.empty()) report(doc, line, "<node> requires id");
                else if (!node_ids.insert(node.id).second) report(doc, line, "duplicate node id: " + node.id);
                doc.nodes.push_back(node);
                node_pushed = true;
            } else {
                node_pushed = false;
            }
            if (tag.name == "mesh" && parent == "node") {
                const auto ref = str_attr(tag, "ref");
                if (ref.empty()) report(doc, line, "<node><mesh> requires ref");
                else if (auto* node = current_node()) {
                    node->mesh_ref = ref;
                    references.push_back({ref, "primitive", line});
                }
            }
            if (tag.name == "channel")
                if (const auto* value = attr(tag, "target")) references.push_back({*value, "target", line});
            if (const auto* value = attr(tag, "assetId")) references.push_back({*value, "asset", line});

            Frame frame{tag.name, line, SIZE_MAX};
            if (tag.name == "node" && node_pushed) frame.node_index = doc.nodes.size() - 1;
            if (!tag.self_closing) {
                stack.push_back(std::move(frame));
                doc.max_depth = std::max(doc.max_depth, stack.size());
            } else if (stack.empty() && root_seen) root_closed = true;
        }
        for (char c : xml.substr(i, end + 1 - i)) if (c == '\n') ++line;
        i = end + 1;
    }

    if (!root_seen) report(doc, 1, "document has no root element");
    if (!stack.empty()) report(doc, stack.back().line, "unclosed tag: " + stack.back().name);
    if (!scene_seen) report(doc, 1, "required <scene> section is missing");
    else if (doc.nodes.empty()) report(doc, scene_line, "<scene> must contain at least one <node>");
    if (doc.max_depth > kMaxDepth)
        report(doc, 1, "XML nesting depth exceeds the limit of " + std::to_string(kMaxDepth) + " (FORMAT.md §17)");

    for (const auto& reference : references) {
        const auto& id = reference.id;
        bool found = false;
        if (reference.kind == "asset") found = asset_ids.contains(id);
        else if (reference.kind == "geometry") found = geometry_ids.contains(id);
        else if (reference.kind == "material") found = material_ids.contains(id);
        else if (reference.kind == "primitive") found = primitive_ids.contains(id);
        else if (reference.kind == "node") found = node_ids.contains(id);
        else if (reference.kind == "buffer") found = buffer_ids.contains(id);
        else if (reference.kind == "bufferView") found = view_ids.contains(id);
        else if (reference.kind == "skeleton") found = skeleton_ids.contains(id);
        else if (reference.kind == "skin") found = skin_ids.contains(id);
        else if (reference.kind == "camera") found = camera_ids.contains(id);
        else if (reference.kind == "light") found = light_ids.contains(id);
        else if (reference.kind == "sampler") found = sampler_ids.contains(id);
        else if (reference.kind == "target") found = node_ids.contains(id) || primitive_ids.contains(id);
        if (!found) report(doc, reference.line, "reference points to unknown " + reference.kind + " id: " + id);
    }

    validate(doc);
    return doc;
}

Document load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Document doc;
        doc.source_path = path;
        report(doc, 1, "cannot open file: " + path.string());
        return doc;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return parse(content.str(), path);
}

} // namespace g3d
