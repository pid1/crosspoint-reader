#include <gtest/gtest.h>

#include "KOReaderIdentifiers.h"

namespace {

TEST(NormalizeMetadataText, FoldsCaseAndCollapsesWhitespace) {
  EXPECT_EQ(KOReaderIdentifiers::normalizeMetadataText("  The\tGreat \n Gatsby  "), "the great gatsby");
  EXPECT_EQ(KOReaderIdentifiers::normalizeMetadataText("THE GREAT GATSBY"),
            KOReaderIdentifiers::normalizeMetadataText("the great gatsby"));
  EXPECT_EQ(KOReaderIdentifiers::normalizeMetadataText(""), "");
  EXPECT_EQ(KOReaderIdentifiers::normalizeMetadataText("   "), "");
}

TEST(NormalizeMetadataText, LeavesNonAsciiBytesAlone) {
  EXPECT_EQ(KOReaderIdentifiers::normalizeMetadataText("Émile Zola"), "\xC3\x89mile zola");
}

TEST(SpineHrefKey, KeepsOnlyTheFileName) {
  EXPECT_EQ(KOReaderIdentifiers::spineHrefKey("OEBPS/Text/chapter1.xhtml"), "chapter1.xhtml");
  EXPECT_EQ(KOReaderIdentifiers::spineHrefKey("chapter1.xhtml"), "chapter1.xhtml");
  EXPECT_EQ(KOReaderIdentifiers::spineHrefKey("OEBPS/Text/chapter1.xhtml#part2"), "chapter1.xhtml");
  EXPECT_EQ(KOReaderIdentifiers::spineHrefKey(""), "");
}

TEST(SpineHrefKey, IgnoresWhereTheOpfSat) {
  EXPECT_EQ(KOReaderIdentifiers::spineHrefKey("OEBPS/Text/ch1.xhtml"), KOReaderIdentifiers::spineHrefKey("ch1.xhtml"));
}

TEST(Flatten, WritesTypeValuePairsInListOrder) {
  const std::vector<KOReaderIdentifier> identifiers = {{"content", "C1"}, {"structure", "S1"}, {"metadata", "M"}};
  EXPECT_EQ(KOReaderIdentifiers::flatten(identifiers), "content:C1,structure:S1,metadata:M");
}

TEST(Flatten, EmptyListProducesNoQuery) { EXPECT_EQ(KOReaderIdentifiers::flatten({}), ""); }

TEST(ProgressTrusted, FollowsTheXPathOnlyForFileLevelMatches) {
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted("content"));
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted("structure"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("metadata"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("filename"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("none"));
}

TEST(ProgressTrusted, ServerWithoutTheFeatureIsAnsweredAsBefore) {
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted(""));
}

}  // namespace
