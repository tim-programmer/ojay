// Shared infrastructure: source files, locations, diagnostics.
#pragma once

#include <cstdint>
#include <deque>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ojay {

struct SourceLoc {
  uint32_t file = 0;
  uint32_t line = 0;  // 1-based; 0 means "no location"
  uint32_t col = 0;   // 1-based
};

struct SourceFile {
  std::string path;
  std::string text;
};

class SourceManager {
 public:
  uint32_t add(std::string path, std::string text) {
    files_.push_back({std::move(path), std::move(text)});
    return static_cast<uint32_t>(files_.size() - 1);
  }
  const SourceFile& get(uint32_t id) const { return files_.at(id); }
  size_t count() const { return files_.size(); }

  std::string describe(SourceLoc loc) const {
    if (loc.line == 0 || loc.file >= files_.size()) return "<unknown>";
    std::ostringstream os;
    os << files_[loc.file].path << ":" << loc.line << ":" << loc.col;
    return os.str();
  }

  // Returns the text of the line containing `loc` (without newline).
  std::string line_text(SourceLoc loc) const {
    if (loc.line == 0 || loc.file >= files_.size()) return {};
    const std::string& t = files_[loc.file].text;
    size_t pos = 0;
    for (uint32_t l = 1; l < loc.line && pos < t.size(); ++pos)
      if (t[pos] == '\n') ++l;
    size_t end = t.find('\n', pos);
    return t.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
  }

 private:
  std::deque<SourceFile> files_;
};

struct CompileError : std::runtime_error {
  SourceLoc loc;
  CompileError(SourceLoc l, const std::string& msg) : std::runtime_error(msg), loc(l) {}
};

template <typename... Args>
std::string cat(Args&&... args) {
  std::ostringstream os;
  (os << ... << args);
  return os.str();
}

template <typename... Args>
[[noreturn]] void fail(SourceLoc loc, Args&&... args) {
  throw CompileError(loc, cat(std::forward<Args>(args)...));
}

}  // namespace ojay
