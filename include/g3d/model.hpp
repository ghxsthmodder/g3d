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
 std::string file; // source path from v1.0 src attribute
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
/// Parse G3D v1.0 and run initial structural checks. This parser supports a
/// restricted XML subset and does not load external section files. A valid
/// result does not imply full semantic conformance with docs/FORMAT.md.
[[nodiscard]] Document parse(std::string_view xml, std::filesystem::path source_path = {});
/// Read a document from disk; I/O errors are returned as diagnostics.
[[nodiscard]] Document load(const std::filesystem::path& path);
} // namespace g3d
