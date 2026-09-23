#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "EpubFixture.h"
#include "HalStorage.h"
#include "KOReaderIdentifiers.h"
#include "KOReaderStructureDigest.h"

namespace {

// The recipe, driven the way the firmware drives it: ContentOpfParser over the
// OPF, KOReaderStructureDigest as the sink.
std::string digestOf(const std::string& opf) {
  fake::reset();
  KOReaderStructureDigest digest;
  const std::string cachePath = "/cache";
  const std::string basePath;
  ContentOpfParser parser(cachePath, basePath, opf.size(), nullptr, false, &digest);
  EXPECT_TRUE(parser.setup());
  EXPECT_EQ(parser.write(reinterpret_cast<const uint8_t*>(opf.data()), opf.size()), opf.size());
  return digest.finish();
}

// md5 of the same lines, assembled here, so a digest assertion below names the
// bytes it stands for rather than a constant nobody can re-derive.
std::string md5Of(const std::string& text) {
  MD5Builder md5;
  md5.begin();
  md5.add(reinterpret_cast<const uint8_t*>(text.data()), text.size());
  md5.calculate();
  return md5.toString();
}

std::string rootfileOf(const std::string& container) {
  ContainerParser parser(container.size());
  EXPECT_TRUE(parser.setup());
  parser.write(reinterpret_cast<const uint8_t*>(container.data()), container.size());
  return parser.fullPath;
}

std::string leavesDir() { return std::string(FIXTURE_DIR) + "/leaves"; }
std::string julietDir() { return std::string(FIXTURE_DIR) + "/juliet"; }

std::string opf(const std::string& before, const std::string& manifest, const std::string& spine) {
  return "<package xmlns=\"http://www.idpf.org/2007/opf\" " + before + "><metadata xmlns:dc=\"urn:dc\">" +
         "<dc:title>T</dc:title></metadata><manifest>" + manifest + "</manifest><spine>" + spine + "</spine></package>";
}

TEST(Flatten, WritesTypeValuePairsInListOrder) {
  const std::vector<KOReaderIdentifier> identifiers = {{"content", "C1"}, {"structure", "S1"}, {"filename", "F1"}};
  EXPECT_EQ(KOReaderIdentifiers::flatten(identifiers), "content:C1,structure:S1,filename:F1");
}

TEST(Flatten, EmptyListProducesNoQuery) { EXPECT_EQ(KOReaderIdentifiers::flatten({}), ""); }

TEST(ProgressTrusted, FollowsTheXPathOnlyForFileLevelMatches) {
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted("content"));
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted("structure"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("filename"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("none"));
}

TEST(ProgressTrusted, UnrecognisedTypesAreNotSufficient) {
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("metadata"));
  EXPECT_FALSE(KOReaderIdentifiers::progressTrusted("edition-2029"));
}

TEST(ProgressTrusted, ServerWithoutTheFeatureIsAnsweredAsBefore) {
  EXPECT_TRUE(KOReaderIdentifiers::progressTrusted(""));
}

TEST(StructureDigest, HashesTheIdentifierThenTheSpineHrefsJoinedByNewlines) {
  const std::string xml = opf(R"(unique-identifier="pid")",
                              R"(<item id="a" href="Text/ch1.xhtml" media-type="application/xhtml+xml"/>)"
                              R"(<item id="b" href="Text/ch2.xhtml" media-type="application/xhtml+xml"/>)",
                              R"(<itemref idref="a"/><itemref idref="b"/>)");
  const std::string withId = xml.substr(0, xml.find("<dc:title>")) +
                             R"(<dc:identifier id="pid">urn:uuid:1</dc:identifier>)" +
                             xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withId), md5Of("urn:uuid:1\nText/ch1.xhtml\nText/ch2.xhtml"));
}

TEST(StructureDigest, TakesTheIdentifierNamedByUniqueIdentifierNotTheFirst) {
  const std::string metadata = R"(<dc:identifier opf:scheme="URI">http://example.invalid/1</dc:identifier>)"
                               R"(<dc:identifier id="pid">urn:uuid:named</dc:identifier>)";
  const std::string xml =
      opf(R"(unique-identifier="pid")", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  const std::string withIds = xml.substr(0, xml.find("<dc:title>")) + metadata + xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withIds), md5Of("urn:uuid:named\nch1.xhtml"));
}

TEST(StructureDigest, FallsBackToTheFirstIdentifierWhenNoneIsNamed) {
  const std::string metadata = R"(<dc:identifier>  urn:uuid:first  </dc:identifier>)"
                               R"(<dc:identifier>urn:uuid:second</dc:identifier>)";
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  const std::string withIds = xml.substr(0, xml.find("<dc:title>")) + metadata + xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withIds), md5Of("urn:uuid:first\nch1.xhtml"));
}

TEST(StructureDigest, FallsBackWhenTheNamedIdentifierIsEmpty) {
  const std::string metadata = R"(<dc:identifier id="pid">   </dc:identifier>)"
                               R"(<dc:identifier></dc:identifier>)"
                               R"(<dc:identifier>urn:uuid:usable</dc:identifier>)";
  const std::string xml =
      opf(R"(unique-identifier="pid")", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  const std::string withIds = xml.substr(0, xml.find("<dc:title>")) + metadata + xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withIds), md5Of("urn:uuid:usable\nch1.xhtml"));
}

TEST(StructureDigest, TakesTheFirstOfSeveralElementsCarryingTheNamedId) {
  const std::string metadata = R"(<dc:identifier id="pid">urn:uuid:one</dc:identifier>)"
                               R"(<dc:identifier id="pid">urn:uuid:two</dc:identifier>)";
  const std::string xml =
      opf(R"(unique-identifier="pid")", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  const std::string withIds = xml.substr(0, xml.find("<dc:title>")) + metadata + xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withIds), md5Of("urn:uuid:one\nch1.xhtml"));
}

TEST(StructureDigest, OmitsTheIdentifierLineWhenThereIsNone) {
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  EXPECT_EQ(digestOf(xml), md5Of("ch1.xhtml"));
}

// Percent escapes, the OPF-relative directory and the fragment are the
// recipe's business; entity references are the parser's, and it expands them.
TEST(StructureDigest, KeepsTheHrefTheParserYields) {
  const std::string xml = opf("",
                              R"(<item id="a" href="Text/a%20b.xhtml"/>)"
                              R"(<item id="b" href="Text/c&amp;d.xhtml"/>)"
                              R"(<item id="c" href="../Text/e.xhtml#part2"/>)",
                              R"(<itemref idref="a"/><itemref idref="b"/><itemref idref="c"/>)");

  EXPECT_EQ(digestOf(xml), md5Of("Text/a%20b.xhtml\nText/c&d.xhtml\n../Text/e.xhtml"));
  EXPECT_EQ(digestOf(xml), "e07ad0e2e24fbaa64b0c40a8b1ebb13f");
}

TEST(StructureDigest, ExpandsEntityReferencesInTheIdentifierToo) {
  const std::string metadata = R"(<dc:identifier id="pid">urn:a&amp;b&#58;1</dc:identifier>)";
  const std::string xml =
      opf(R"(unique-identifier="pid")", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="a"/>)");
  const std::string withId = xml.substr(0, xml.find("<dc:title>")) + metadata + xml.substr(xml.find("<dc:title>"));

  EXPECT_EQ(digestOf(withId), md5Of("urn:a&b:1\nch1.xhtml"));
  EXPECT_EQ(digestOf(withId), "fb3ed76af6e07f28456616a77330b19f");
}

TEST(StructureDigest, MatchesElementsOnTheirLocalName) {
  const std::string prefixed =
      R"(<opf:package xmlns:opf="http://www.idpf.org/2007/opf" unique-identifier="pid">)"
      R"(<opf:metadata xmlns="http://purl.org/dc/elements/1.1/"><identifier id="pid">urn:uuid:9</identifier>)"
      R"(</opf:metadata><opf:manifest><opf:item id="a" href="ch1.xhtml"/></opf:manifest>)"
      R"(<opf:spine><opf:itemref idref="a"/></opf:spine></opf:package>)";

  EXPECT_EQ(digestOf(prefixed), md5Of("urn:uuid:9\nch1.xhtml"));
}

TEST(StructureDigest, SkipsAnItemrefThatResolvesToNoManifestItem) {
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/><item id="b" href="ch2.xhtml"/>)",
                              R"(<itemref idref="a"/><itemref idref="gone"/><itemref idref="b"/>)");

  EXPECT_EQ(digestOf(xml), md5Of("ch1.xhtml\nch2.xhtml"));
}

TEST(StructureDigest, SkipsAnItemThatCarriesNoHref) {
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/><item id="b"/><item id="c" href="ch2.xhtml"/>)",
                              R"(<itemref idref="a"/><itemref idref="b"/><itemref idref="c"/>)");

  EXPECT_EQ(digestOf(xml), md5Of("ch1.xhtml\nch2.xhtml"));
}

TEST(StructureDigest, CountsNonLinearSpineEntries) {
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/><item id="b" href="notes.xhtml"/>)",
                              R"(<itemref idref="a"/><itemref idref="b" linear="no"/>)");

  EXPECT_EQ(digestOf(xml), md5Of("ch1.xhtml\nnotes.xhtml"));
}

TEST(StructureDigest, IgnoresHrefsOutsideTheManifestAndSpine) {
  const std::string xml = R"(<package xmlns="http://www.idpf.org/2007/opf"><metadata xmlns:dc="urn:dc"/><manifest>)"
                          R"(<item id="a" href="ch1.xhtml"/></manifest><spine><itemref idref="a"/></spine>)"
                          R"(<guide><reference type="cover" href="cover.xhtml"/></guide></package>)";

  EXPECT_EQ(digestOf(xml), md5Of("ch1.xhtml"));
}

TEST(StructureDigest, IsEmptyWithoutASpine) {
  const std::string xml =
      R"(<package xmlns="http://www.idpf.org/2007/opf" unique-identifier="pid"><metadata xmlns:dc="urn:dc">)"
      R"(<dc:identifier id="pid">urn:uuid:1</dc:identifier></metadata>)"
      R"(<manifest><item id="a" href="ch1.xhtml"/></manifest></package>)";

  EXPECT_EQ(digestOf(xml), "");
}

TEST(StructureDigest, IsEmptyWhenTheSpineNamesNothingThatResolves) {
  const std::string xml = opf("", R"(<item id="a" href="ch1.xhtml"/>)", R"(<itemref idref="gone"/>)");
  EXPECT_EQ(digestOf(xml), "");
}

TEST(Rootfile, PrefersTheFirstPackageDocument) {
  const std::string container = R"(<container><rootfiles>)"
                                R"(<rootfile full-path="other.xml" media-type="application/x-dtbook+xml"/>)"
                                R"(<rootfile full-path="OPS/first.opf" media-type="application/oebps-package+xml"/>)"
                                R"(<rootfile full-path="OPS/second.opf" media-type="application/oebps-package+xml"/>)"
                                R"(</rootfiles></container>)";

  EXPECT_EQ(rootfileOf(container), "OPS/first.opf");
}

TEST(Rootfile, FallsBackToTheFirstOfAnyTypeWhenNoneDeclaresThePackage) {
  const std::string container =
      R"(<ocf:container xmlns:ocf="urn:oasis:names:tc:opendocument:xmlns:container"><ocf:rootfiles>)"
      R"(<ocf:rootfile full-path="only.opf"/>)"
      R"(<ocf:rootfile full-path="later.opf"/>)"
      R"(</ocf:rootfiles></ocf:container>)";

  EXPECT_EQ(rootfileOf(container), "only.opf");
}

// ---------------------------------------------------------------------------
// The two real books, through the whole path: container.xml, then the OPF.

class StructureDigestFixtures : public ::testing::Test {
 protected:
  void SetUp() override { fake::reset(); }
};

// Leaves of Grass: the uuid `unique-identifier` names, then 383 spine hrefs.
// 384 lines, 31631 bytes. The same digest the KOReader implementation of the
// recipe reports for this file.
TEST_F(StructureDigestFixtures, DigestsLeavesOfGrass) {
  const std::string archive = fixture::build(leavesDir(), "content.opf").pack();
  EXPECT_EQ(fixture::structureDigest(archive), "3d550f5e63e45c11087f7548bd4bc37c");
}

// Romeo and Juliet: one identifier and 30 spine hrefs, 31 lines, 365 bytes.
TEST_F(StructureDigestFixtures, DigestsRomeoAndJuliet) {
  const std::string archive = fixture::build(julietDir(), "OPS/fb.opf").pack();
  EXPECT_EQ(fixture::structureDigest(archive), "2cdc5c1ff1de87d71a4f7f57ec8744da");
}

TEST_F(StructureDigestFixtures, TheTwoBooksDiffer) {
  const std::string leaves = fixture::structureDigest(fixture::build(leavesDir(), "content.opf").pack());
  const std::string juliet = fixture::structureDigest(fixture::build(julietDir(), "OPS/fb.opf").pack());

  EXPECT_FALSE(leaves.empty());
  EXPECT_NE(leaves, juliet);
}

TEST_F(StructureDigestFixtures, SurvivesARepack) {
  const fixture::Epub epub = fixture::build(leavesDir(), "content.opf");
  const std::string fast = epub.pack(MZ_BEST_SPEED);
  const std::string small = epub.pack(MZ_BEST_COMPRESSION);

  EXPECT_NE(fast, small);
  EXPECT_EQ(fixture::structureDigest(fast), fixture::structureDigest(small));
}

TEST_F(StructureDigestFixtures, SurvivesImageReEncoding) {
  fixture::Epub epub = fixture::build(leavesDir(), "content.opf");
  const std::string before = fixture::structureDigest(epub.pack());

  // What the EPUB optimizer does to a cover: JPEG bytes through a canvas, and
  // the manifest's media-type corrected to match. Neither is a spine entry.
  epub.put("cover.jpeg", std::string("\xFF\xD8\xFF\xE0re-encoded", 15));
  std::string* opf = epub.find("content.opf");
  ASSERT_NE(opf, nullptr);
  const size_t at = opf->find("media-type=\"image/jpeg\"");
  ASSERT_NE(at, std::string::npos);
  opf->replace(at, std::string("media-type=\"image/jpeg\"").size(), "media-type=\"image/png\" ");

  EXPECT_EQ(fixture::structureDigest(epub.pack()), before);
}

TEST_F(StructureDigestFixtures, SurvivesContentInjectedIntoEveryChapter) {
  fixture::Epub epub = fixture::build(leavesDir(), "content.opf");
  const std::string before = fixture::structureDigest(epub.pack());

  for (auto& [name, bytes] : epub.entries) {
    if (name.size() > 5 && name.compare(name.size() - 5, 5, ".html") == 0) {
      bytes = "<html><head><link rel=\"stylesheet\" href=\"crosspoint.css\"/></head>" + bytes + "</html>";
    }
  }

  EXPECT_EQ(fixture::structureDigest(epub.pack()), before);
}

TEST_F(StructureDigestFixtures, ChangesWhenASpineEntryIsReordered) {
  fixture::Epub epub = fixture::build(julietDir(), "OPS/fb.opf");
  const std::string before = fixture::structureDigest(epub.pack());

  std::string* opf = epub.find("OPS/fb.opf");
  ASSERT_NE(opf, nullptr);
  const size_t first = opf->find("<itemref idref=\"main0\"");
  const size_t second = opf->find("<itemref idref=\"main1\"");
  ASSERT_NE(first, std::string::npos);
  ASSERT_NE(second, std::string::npos);
  opf->replace(second, 22, "<itemref idref=\"main0\"");
  opf->replace(first, 22, "<itemref idref=\"main1\"");

  EXPECT_NE(fixture::structureDigest(epub.pack()), before);
}

TEST_F(StructureDigestFixtures, ChangesWhenASpineEntryIsRemoved) {
  fixture::Epub epub = fixture::build(julietDir(), "OPS/fb.opf");
  const std::string before = fixture::structureDigest(epub.pack());

  std::string* opf = epub.find("OPS/fb.opf");
  ASSERT_NE(opf, nullptr);
  const size_t at = opf->find("<itemref idref=\"main0\"");
  ASSERT_NE(at, std::string::npos);
  const size_t end = opf->find('>', at);
  opf->erase(at, end - at + 1);

  EXPECT_NE(fixture::structureDigest(epub.pack()), before);
}

}  // namespace
