#pragma once

#include <string>
#include <string_view>

namespace trading {

// Owns untrusted text and encodes it for HTML text/double-quoted attributes
// or one quoted CSV field. It does not interpret markup or financial values.
class ReportText {
public:
    explicit ReportText(std::string_view text) : text_{text} {}

    [[nodiscard]] std::string html() const {
        std::string result;
        for (const char character : text_) {
            switch (character) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            default: result += character;
            }
        }
        return result;
    }

    [[nodiscard]] std::string csv() const {
        std::string result{"\""};
        for (const char character : text_) {
            if (character == '"') { result += '"'; }
            result += character;
        }
        return result + '"';
    }

private:
    std::string text_;
};

} // namespace trading
