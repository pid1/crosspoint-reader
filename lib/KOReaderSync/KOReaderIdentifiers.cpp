#include "KOReaderIdentifiers.h"

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
