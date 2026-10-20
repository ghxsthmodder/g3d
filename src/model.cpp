#include "g3d/model.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <unordered_map>
#include <utility>

namespace g3d {
namespace {

struct Tag {
    std::string name;
    std::unordered_map<std::string, std::string> attrs;
    bool closing{false};
    bool self_closing{false};
    std::size_t line{1};
};

bool name_char(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_' || c == '-' || c == ':' || c == '.';
}

void diagnostic(Document& doc, std::size_t line, std::string message,
                Diagnostic::Severity severity = Diagnostic::Severity::error) {
    doc.diagnostics.push_back({severity, line, std::move(message)});
}

bool parse_tag(std::string_view raw, std::size_t line, Tag& out,
               std::string& error) {
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

} // namespace

bool Document::valid() const noexcept {
    return std::none_of(diagnostics.begin(), diagnostics.end(), [](const Diagnostic& d) {
        return d.severity == Diagnostic::Severity::error;
    });
}

Document parse(std::string_view xml, std::filesystem::path source_path) {
    Document doc;
    doc.source_path = std::move(source_path);
    std::vector<std::pair<std::string, std::size_t>> stack;
    std::set<std::string> asset_ids;
    std::vector<std::pair<std::string, std::size_t>> references;
    bool root_seen = false, root_closed = false, scene_seen = false;
    std::size_t line = 1, i = 0;

    while (i < xml.size()) {
        if (xml[i] == '\n') { ++line; ++i; continue; }
        if (xml[i] != '<') {
            const auto next = xml.find('<', i);
            const auto end = next == std::string_view::npos ? xml.size() : next;
            const auto text = xml.substr(i, end - i);
            if (text.find("]]>") != std::string_view::npos)
                diagnostic(doc, line, "']]>' is not allowed in ordinary text");
            bool entities_ok = true;
            (void)decode_entities(text, entities_ok);
            if (!entities_ok) diagnostic(doc, line, "unknown or malformed XML entity");
            for (char c : text) if (c == '\n') ++line;
            i = end;
            continue;
        }

        if (xml.substr(i, 4) == "<!--") {
            const auto end = xml.find("-->", i + 4);
            if (end == std::string_view::npos) { diagnostic(doc, line, "unterminated comment"); break; }
            if (xml.substr(i + 4, end - i - 4).find("--") != std::string_view::npos)
                diagnostic(doc, line, "double hyphen is not allowed inside comments");
            for (char c : xml.substr(i, end + 3 - i)) if (c == '\n') ++line;
            i = end + 3;
            continue;
        }
        if (xml.substr(i, 2) == "<?") {
            const auto end = xml.find("?>", i + 2);
            if (end == std::string_view::npos) { diagnostic(doc, line, "unterminated processing instruction"); break; }
            auto pi = xml.substr(i + 2, end - i - 2);
            if (!pi.starts_with("xml") || (pi.size() > 3 && !std::isspace(static_cast<unsigned char>(pi[3])))) {
                diagnostic(doc, line, "processing instructions are not supported in G3D v1.0");
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
            diagnostic(doc, line, "DOCTYPE and declarations are not supported by the initial parser");
            const auto end = xml.find('>', i + 2);
            if (end == std::string_view::npos) break;
            i = end + 1;
            continue;
        }

        const auto end = xml.find('>', i + 1);
        if (end == std::string_view::npos) { diagnostic(doc, line, "unterminated tag"); break; }
        Tag tag;
        std::string error;
        if (!parse_tag(xml.substr(i + 1, end - i - 1), line, tag, error)) {
            diagnostic(doc, line, error);
            i = end + 1;
            continue;
        }
        if (tag.closing) {
            if (stack.empty()) diagnostic(doc, line, "closing tag without matching opener: " + tag.name);
            else if (stack.back().first != tag.name) {
                diagnostic(doc, line, "mismatched closing tag: " + tag.name +
                            " (expected " + stack.back().first + ")");
                stack.pop_back();
            } else stack.pop_back();
            if (stack.empty() && root_seen) root_closed = true;
        } else {
            const bool is_root = !root_seen;
            if (!root_seen) {
                root_seen = true;
                if (tag.name != "g3d") diagnostic(doc, line, "root element must be <g3d>");
                auto v = tag.attrs.find("version");
                if (v == tag.attrs.end() || v->second.empty()) diagnostic(doc, line, "<g3d> requires version=\"1.0\"");
                else if (v->second != "1.0") diagnostic(doc, line, "unsupported G3D version: " + v->second);
                else doc.version = v->second;
            } else if (root_closed || stack.empty()) {
                diagnostic(doc, line, "multiple root elements");
            }
            if (!is_root && stack.size() == 1) {
                if (tag.name == "scene") scene_seen = true;
                const std::vector<std::string> allowed = {"metadata","assets","mesh","materials","animations","collisions","scene"};
                if (std::find(allowed.begin(), allowed.end(), tag.name) == allowed.end())
                    diagnostic(doc, line, "unknown top-level section: " + tag.name);
            }
            if (tag.name == "asset") {
                const auto id = tag.attrs.find("id");
                const auto type = tag.attrs.find("type");
                const auto src = tag.attrs.find("src");
                if (id == tag.attrs.end() || id->second.empty()) diagnostic(doc, line, "<asset> requires a non-empty id");
                else if (!asset_ids.insert(id->second).second) diagnostic(doc, line, "duplicate asset id: " + id->second);
                if (type == tag.attrs.end() || type->second != "texture") diagnostic(doc, line, "<asset> type must be texture in v1.0");
                if (src == tag.attrs.end() || src->second.empty()) diagnostic(doc, line, "<asset> requires src");
                Asset asset;
                if (id != tag.attrs.end()) asset.id = id->second;
                if (type != tag.attrs.end()) asset.type = type->second;
                if (src != tag.attrs.end()) asset.file = src->second;
                if (auto rp = tag.attrs.find("relativePath"); rp != tag.attrs.end())
                    asset.relative_path = rp->second != "false" && rp->second != "0";
                doc.assets.push_back(std::move(asset));
            }
            if (tag.name == "mesh" || tag.name == "animations" || tag.name == "collisions" ||
                tag.name == "materials" || tag.name == "assets") {
                if (auto ext = tag.attrs.find("src"); ext != tag.attrs.end()) doc.external_files.push_back(ext->second);
            }
            if (auto ref = tag.attrs.find("assetId"); ref != tag.attrs.end())
                references.emplace_back(ref->second, line);
            if (!tag.self_closing) stack.emplace_back(tag.name, line);
            else if (stack.empty() && root_seen) root_closed = true;
        }
        for (char c : xml.substr(i, end + 1 - i)) if (c == '\n') ++line;
        i = end + 1;
    }

    if (!root_seen) diagnostic(doc, 1, "document has no root element");
    if (!stack.empty()) diagnostic(doc, stack.back().second, "unclosed tag: " + stack.back().first);
    if (!scene_seen) diagnostic(doc, 1, "required <scene> section is missing");
    for (const auto& [id, ref_line] : references)
        if (!asset_ids.contains(id)) diagnostic(doc, ref_line, "reference points to unknown asset id: " + id);
    return doc;
}

Document load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        Document doc;
        doc.source_path = path;
        diagnostic(doc, 1, "cannot open file: " + path.string());
        return doc;
    }
    std::ostringstream content;
    content << file.rdbuf();
    return parse(content.str(), path);
}

} // namespace g3d
