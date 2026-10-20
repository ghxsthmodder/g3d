#include "g3d/model.hpp"

#include <iostream>
#include <string>

namespace {
void usage() {
    std::cout << "G3D tools 0.1.0\n"
              << "Usage:\n"
              << "  g3d-tool validate <file.g3d>  Check the rules that need no bytes (FORMAT.md §2–§17,\n"
              << "                                BINARY.md §3–§7); pool and KTX2 bytes are not read\n"
              << "  g3d-tool inspect <file.g3d>   Print document summary\n"
              << "  g3d-tool --help               Show this help\n";
}
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        usage();
        return 0;
    }
    if (argc != 3) { usage(); return 2; }

    const std::string command = argv[1];
    if (command != "validate" && command != "inspect") {
        std::cerr << "error: unknown command: " << command << "\n";
        usage();
        return 2;
    }

    const auto doc = g3d::load(argv[2]);
    for (const auto& d : doc.diagnostics) {
        const char* level = d.severity == g3d::Diagnostic::Severity::error ? "error" : "warning";
        std::cerr << argv[2] << ':' << d.line << ": " << level << ": " << d.message << '\n';
    }

    if (command == "inspect") {
        std::cout << "G3D document: " << doc.source_path.string() << '\n'
                  << "Version: " << (doc.version.empty() ? "(unspecified)" : doc.version) << '\n'
                  << "Assets: " << doc.assets.size() << '\n'
                  << "External sidecars: " << doc.external_files.size() << '\n';
        for (const auto& asset : doc.assets)
            std::cout << "  asset id=" << asset.id << " type=" << asset.type
                      << (asset.file.empty() ? "" : " file=" + asset.file) << '\n';
    }

    if (!doc.valid()) return 1;
    if (command == "validate") std::cout << "Valid G3D document: " << argv[2] << '\n';
    return 0;
}
