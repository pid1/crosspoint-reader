#include "KOReaderIdentifiers.h"

namespace {
bool isSpace(const char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
}  // namespace

std::string KOReaderIdentifiers::normalizeMetadataText(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  bool pendingSpace = false;
  for (const char c : text) {
    if (isSpace(c)) {
      pendingSpace = !normalized.empty();
      continue;
    }
    if (pendingSpace) {
      normalized.push_back(' ');
      pendingSpace = false;
    }
    normalized.push_back(c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c);
  }
  return normalized;
}

std::string_view KOReaderIdentifiers::spineHrefKey(const std::string& href) {
  std::string_view key{href};
  // A fragment belongs to the link, not to the file.
  const size_t hash = key.find('#');
  if (hash != std::string_view::npos) {
    key = key.substr(0, hash);
  }
  const size_t slash = key.rfind('/');
  if (slash != std::string_view::npos) {
    key = key.substr(slash + 1);
  }
  return key;
}

std::string KOReaderIdentifiers::flatten(const std::vector<KOReaderIdentifier>& identifiers) {
  std::string flattened;
  for (const auto& identifier : identifiers) {
    if (!flattened.empty()) {
      flattened.push_back(',');
    }
    flattened.append(identifier.type);
    flattened.push_back(':');
    flattened.append(identifier.value);
  }
  return flattened;
}

bool KOReaderIdentifiers::progressTrusted(const std::string& progressMatch) {
  return progressMatch.empty() || progressMatch == TYPE_CONTENT || progressMatch == TYPE_STRUCTURE;
}
