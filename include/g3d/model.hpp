#pragma once

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

struct Asset {
    std::string id;
    std::string type;
    std::string file;
    bool relative_path{true};
};

struct Document {
    std::filesystem::path source_path;
    std::string version;
    std::vector<Asset> assets;
    std::vector<std::string> external_files;
    std::vector<Diagnostic> diagnostics;

    [[nodiscard]] bool valid() const noexcept;
};

/// Parse a G3D XML document from a string and perform initial structural checks.
/// This first-stage parser intentionally supports a restricted XML subset; it
/// rejects DTDs and does not attempt to expand external entities.
[[nodiscard]] Document parse(std::string_view xml,
                             std::filesystem::path source_path = {});

/// Read a document from disk and run the same checks. File I/O errors are
/// returned as diagnostics rather than thrown.
[[nodiscard]] Document load(const std::filesystem::path& path);

} // namespace g3d
