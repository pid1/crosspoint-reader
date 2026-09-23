#pragma once
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/**
 * One entry of the optional `identifiers` list (koreader-sync-server PR #55).
 *
 * `type` is an opaque label the server stores and echoes without interpreting.
 * A list is ordered by the client's preference, strongest first, and its first
 * `value` is the `document` the request addresses.
 */
struct KOReaderIdentifier {
  std::string type;
  std::string value;
};

namespace KOReaderIdentifiers {

// A longer list is rejected with 403 / code 2003.
constexpr size_t MAX_ENTRIES = 8;

// Type labels, strongest (pins this file) to weakest (names the work).
constexpr char TYPE_CONTENT[] = "content";      // Partial MD5 of the file bytes
constexpr char TYPE_STRUCTURE[] = "structure";  // Title, author and spine layout
constexpr char TYPE_METADATA[] = "metadata";    // Title and author
constexpr char TYPE_FILENAME[] = "filename";    // MD5 of the basename

/**
 * Folds case, collapses runs of whitespace and trims, so that two copies whose
 * OPF differs only in the spelling of its whitespace hash alike. Bytes above
 * ASCII pass through untouched.
 */
std::string normalizeMetadataText(const std::string& text);

/**
 * The part of a spine href that survives being repacked: the file name, with
 * the directory the OPF happened to sit in and any fragment dropped. What an
 * xpointer's DocFragment index counts is this list, in this order.
 */
std::string_view spineHrefKey(const std::string& href);

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
 * the ones that xpointer was written against; anything weaker is the same work
 * in another file, where the path may resolve to the wrong place or not at all
 * and only the percentage carries over. An empty string is a server that does
 * not implement the feature, and its answer is followed as before.
 */
bool progressTrusted(const std::string& progressMatch);

}  // namespace KOReaderIdentifiers
