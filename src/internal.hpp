#pragma once
// Declarations shared by the parser and the validator. Not a public header: the public
// surface is include/g3d/model.hpp.
#include "g3d/model.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace g3d {
void report(Document& doc, std::size_t line, std::string message,
            Diagnostic::Severity severity = Diagnostic::Severity::error);

/// A complete, finite decimal number — `end` must reach the end of the text, so "1.5x"
/// and "0.1.2" are rejected rather than silently truncated.
bool number_text(std::string_view text, double& out);

/// Semantic rules that need no bytes: value domains, §17 limits, and the arithmetic
/// between declarations. Called by parse() once the document is collected.
void validate(Document& doc);
} // namespace g3d
