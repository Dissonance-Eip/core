#pragma once

#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace fixture {

/**
 * @brief Captures everything written to std::cout while in scope.
 *
 * Restores both the original buffer and the stream's formatting on
 * destruction, since some commands switch std::cout to fixed precision.
 */
class CoutCapture {
  public:
    CoutCapture() {
        saved_.copyfmt(std::cout);
        original_ = std::cout.rdbuf(buffer_.rdbuf());
    }

    ~CoutCapture() {
        std::cout.rdbuf(original_);
        std::cout.copyfmt(saved_);
    }

    CoutCapture(const CoutCapture &) = delete;
    CoutCapture &operator=(const CoutCapture &) = delete;

    /** @brief Output exactly as written, colour codes included. */
    [[nodiscard]] std::string raw() const { return buffer_.str(); }

    /** @brief Output with ANSI colour codes removed. */
    [[nodiscard]] std::string text() const {
        static const std::regex ansi("\033\\[[0-9;]*m");
        return std::regex_replace(buffer_.str(), ansi, "");
    }

  private:
    std::ostringstream buffer_;
    std::ios saved_{nullptr};
    std::streambuf *original_ = nullptr;
};

} // namespace fixture
