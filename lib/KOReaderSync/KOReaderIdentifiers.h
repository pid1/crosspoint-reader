#pragma once
#include <cstddef>
#include <string>
#include <vector>

/**
 * One entry of the optional `identifiers` list (koreader-sync-server PR #55).
 *
 * `type` is a label the server stores and echoes without interpreting; clients
 * share a recipe per label, since a match on one is what decides whether
 * another device's position can be followed. A list is ordered by the client's
 * preference, strongest first, and contains an entry whose `value` equals the
 * `document` the request addresses.
 */
struct KOReaderIdentifier {
  std::string type;
  std::string value;
};

namespace KOReaderIdentifiers {

// A longer list is rejected with 403 / code 2003.
constexpr size_t MAX_ENTRIES = 8;

// Type labels, strongest (pins this file) to weakest (names the copy).
constexpr char TYPE_CONTENT[] = "content";      // Partial MD5 of the file bytes
constexpr char TYPE_STRUCTURE[] = "structure";  // MD5 over the package identifier and the spine
constexpr char TYPE_FILENAME[] = "filename";    // MD5 of the file name

/**
 * `ids=` query value for GET /syncs/progress/:document, which has no body:
 * `type:value,type:value` in list order. Entries are hex digests and labels
 * matching `^[a-z][a-z0-9-]*$`, neither of which needs escaping.
 */
std::string flatten(const std::vector<KOReaderIdentifier>& identifiers);

/**
 * Whether a stored `progress` xpointer addresses this copy of the book.
 *
 * `progress_match` names the strongest identifier shared with whoever wrote the
 * stored position. `content` and `structure` mean the spine and its elements are
 * the ones that xpointer was written against; any other label, recognised or
 * not, carries no such guarantee and only the percentage carries over
 * ([K-ID-14]). An empty string is a server that does not implement the feature,
 * and its answer is followed as before.
 */
bool progressTrusted(const std::string& progressMatch);

}  // namespace KOReaderIdentifiers
