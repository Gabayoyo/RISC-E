#pragma once

#include <iosfwd>
#include <string_view>

// One block of the CLI's post-run report. Sections are composed in main and
// printed in order, so new breakdowns (memory, instruction mix, ...) only need
// a new subclass and a single registration in main.
class ReportSection {
public:
    virtual ~ReportSection() = default;

    // A short heading printed on its own line before the body.
    virtual std::string_view title() const = 0;

    // Writes the indented body lines (no title, no trailing blank line).
    virtual void render(std::ostream& out) const = 0;
};
