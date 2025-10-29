#include "fuzzniq/matcher.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace fuzzniq {
namespace {

using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using ::testing::MatchesRegex;

void MergeInput(const std::vector<std::string_view>& in_strs, std::ostream& out,
                char delim = '\n') {
  for (const auto& s : in_strs) {
    out << s << delim;
  }
}

std::vector<std::string> SplitOutput(const std::string& str,
                                     char delim = '\n') {
  std::vector<std::string> result;
  if (str.empty()) {
    return result;
  }
  auto p_beg = str.begin();
  for (auto p_next = std::next(p_beg); p_next < str.end(); ++p_next) {
    if (*p_next == delim) {
      result.emplace_back(&(*p_beg), p_next - p_beg);
      p_beg = std::next(p_next);
      ++p_next;
    }
  }
  return result;
}

std::vector<std::string> RunInBatch(
    const std::vector<std::string_view>& in_strs, Matcher& m,
    char delim = '\n') {
  m.AddInputLines(in_strs);
  return SplitOutput(m.ConsumeAllOutputs());
}

std::vector<std::string> RunIncrementally(
    const std::vector<std::string_view>& in_strs, Matcher& m,
    char delim = '\n') {
  std::stringstream in_ss;
  MergeInput(in_strs, in_ss);
  std::stringstream out_ss;
  while (in_ss) {
    in_ss >> m;
    out_ss << m;
  }
  out_ss << m.Flush();
  return SplitOutput(out_ss.str());
}

TEST(Matcher, Basic) {
  Matcher m{MatcherParameters{}};
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  EXPECT_EQ(m.ConsumeAllOutputs(), "A\n");
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  m.AddInputLines(std::array{"A", "A", "A", "A", "A"});
  EXPECT_EQ(m.ConsumeAllOutputs(), "A\n");
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, Reset) {
  Matcher m{MatcherParameters{}};
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.AddInputLine("A");
  m.Reset();
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  m.AddInputLines(std::array{"A", "A", "A", "A", "A"});
  EXPECT_EQ(m.ConsumeAllOutputs(), "A\n");
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, NullData) {
  Matcher m{MatcherParameters{.null_data = true}};
  m.AddInputLines(std::array{"A", "A", "A", "A", "A"});
  std::string anull;
  anull.push_back('A');
  anull.push_back('\0');
  EXPECT_EQ(m.ConsumeAllOutputs(), anull);
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, Count) {
  Matcher m{MatcherParameters{.print_count = true}};

  EXPECT_THAT(RunInBatch({"A", "A", "A", "A", "A"}, m),
              ElementsAreArray({MatchesRegex("\\s*5\\s*A")}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "B", "A", "A"}, m),
              ElementsAreArray({MatchesRegex("\\s*1\\s*A"),
                                MatchesRegex("\\s*2\\s*B.*"),
                                MatchesRegex("\\s*2\\s*A.*")}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, SkipMarker) {
  Matcher m{MatcherParameters{.skip_marker = "S"}};

  EXPECT_THAT(RunInBatch({"A", "A", "A", "A", "A"}, m),
              ElementsAreArray({"S", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "B", "A", "A"}, m),
              ElementsAreArray({"A", "S", "B", "S", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, TwoLines) {
  Matcher m{MatcherParameters{.line_count = 2}};

  EXPECT_THAT(RunInBatch({"A", "B", "A", "B", "A"}, m),
              ElementsAreArray({"B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "A", "B", "B", "A"}, m),
              ElementsAreArray({"A", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, TwoStrictOrder) {
  Matcher m{MatcherParameters{.line_count = 2, .strict_order = true}};

  EXPECT_THAT(RunInBatch({"A", "B", "A", "B", "A"}, m),
              ElementsAreArray({"B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "A", "B", "B", "A"}, m),
              ElementsAreArray({"A", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, ThreeLines) {
  Matcher m{MatcherParameters{.line_count = 3}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "B", "C", "A"}, m),
              ElementsAreArray({"A", "B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "C", "B", "A"}, m),
              ElementsAreArray({"A", "C", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, ThreeStrictOrder) {
  Matcher m{MatcherParameters{.line_count = 3, .strict_order = true}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "B", "C", "A"}, m),
              ElementsAreArray({"A", "B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "C", "B", "A"}, m),
              ElementsAreArray({"A", "B", "C", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, ThreeStrictSeq) {
  Matcher m{MatcherParameters{.line_count = 3, .strict_seq = true}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "B", "C", "A"}, m),
              ElementsAreArray({"A", "B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "C", "C"}, m),
              ElementsAreArray({"B", "C", "A", "C"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "C", "B", "A"}, m),
              ElementsAreArray({"A", "B", "C", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, SixLines) {
  Matcher m{MatcherParameters{.line_count = 6}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "F", "A", "B", "C", "D", "E",
                          "F", "A", "B", "C", "D", "E", "F"},
                         m),
              ElementsAreArray({"A", "B", "C", "D", "E", "F"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "C", "B", "E", "D", "A"}, m),
              ElementsAreArray({"A", "C", "B", "E", "D", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, SixLinesIncremental) {
  Matcher m{MatcherParameters{.line_count = 6}};
  EXPECT_THAT(RunIncrementally({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));

  EXPECT_THAT(RunIncrementally({"A", "B", "C", "D", "E", "F", "A", "B", "C",
                                "D", "E", "F", "A", "B", "C", "D", "E", "F"},
                               m),
              ElementsAreArray({"A", "B", "C", "D", "E", "F"}));

  EXPECT_THAT(
      RunIncrementally({"A", "B", "C", "D", "E", "C", "B", "E", "D", "A"}, m),
      ElementsAreArray({"A", "C", "B", "E", "D", "A"}));
}

TEST(Matcher, SixStrictOrder) {
  Matcher m{MatcherParameters{.line_count = 6, .strict_order = true}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "F", "A", "B", "C", "D", "E",
                          "F", "A", "B", "C", "D", "E", "F"},
                         m),
              ElementsAreArray({"A", "B", "C", "D", "E", "F"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "C", "B", "E", "D", "A"}, m),
              ElementsAreArray({"A", "B", "D", "E", "C", "B", "E", "D", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "A", "C", "B", "A"}, m),
              ElementsAreArray({"B", "D", "A", "C", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

TEST(Matcher, SixStrictSeq) {
  Matcher m{MatcherParameters{.line_count = 6, .strict_seq = true}};

  EXPECT_THAT(RunInBatch({"A", "B", "C", "A", "B", "C", "A"}, m),
              ElementsAreArray({"B", "C", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "F", "A", "B", "C", "D", "E",
                          "F", "A", "B", "C", "D", "E", "F"},
                         m),
              ElementsAreArray({"A", "B", "C", "D", "E", "F"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "E", "C", "B", "E", "D", "A"}, m),
              ElementsAreArray({"A", "B", "D", "E", "C", "B", "E", "D", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());

  EXPECT_THAT(RunInBatch({"A", "B", "C", "D", "A", "C", "B", "A"}, m),
              ElementsAreArray({"B", "C", "D", "A", "C", "B", "A"}));
  EXPECT_THAT(m.ConsumeAllOutputs(), IsEmpty());
}

}  // namespace
}  // namespace fuzzniq
