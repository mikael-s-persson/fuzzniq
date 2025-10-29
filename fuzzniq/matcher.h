#ifndef FUZZNIQ_MATCHER_H_
#define FUZZNIQ_MATCHER_H_

#include <cassert>
#include <cstdint>
#include <deque>
#include <iostream>
#include <limits>
#include <regex>
#include <string>
#include <type_traits>

namespace fuzzniq {

// Supported edit-distance metrics.
// See wikipedia or vibe with your favorite LLM to learn more.
enum class Method : std::uint8_t {
  kNormalizedLevenshtein = 0,
  kLevenshtein = 1,
  kNormalizedHamming = 2,
  kHamming = 3,
};

struct MatcherParameters {
  std::regex input_regex{".*"};
  std::string input_regex_fmt;
  std::regex_constants::match_flag_type input_regex_flags =
      std::regex_constants::match_default;
  std::string skip_marker;
  Method method = Method::kNormalizedLevenshtein;
  float threshold = 0.1F;
  int line_count = 1;
  bool ignore_case = false;
  bool print_count = false;
  bool print_score = false;
  bool null_data = false;
  bool strict_order = false;
  bool strict_seq = false;
};

// This class maintains a short window of pending lines to be filtered
// according to fuzzy matching of groups of multiple lines of text.
//
// The fuzzy matching (or approximate string matching) uses one of a few
// possible edit-distance metrics and a configurable threshold to classify
// two lines as being a good enough match.
//
// With a window of 1 line, this class is essentially a fuzzy `uniq` tool,
// filtering out approximately-duplicated consecutive lines.
//
// With longer windows, things get a bit trickier (and more useful). The
// general idea is to filter out approximately-repeating patterns. For example,
// text logs that contain long sections of mundane and repetitive entries
// that vary slightly, no just in terms of edit-distance but also in the
// order or permutations of the repeating entries.
//
// There are three options for multi-line pattern matching. First, by default,
// any line that matches a prior line within the window causes that prior
// line to be filtered out. This implies that any ordering or sequencing of
// the matching lines is ignored. Second, one can enable `strict_order`,
// which additionally requires matches to be "in-order", meaning that the
// prior line to which a new line is matching should not precede a line
// that has already been matched-with (aka filtered out). Finally, one
// can enable `strict_seq` which requires that matching pairs form a
// repeating sequence. Consider the following examples, using matching
// letters to represent matching lines, and D, O, and X, for default,
// ordered and sequenced matches, for a window of 3 lines, and using []
// to mark a contiguous group of matched multi-line patter:
//  - ABCABC: D: [ABCABC] O: [ABCABC] X: [ABCABC]
//  - ABCAAA: D: [ABCAAA] O: [ABCA][AA] X: [ABCA][AA]
//  - ABCACA: D: [ABCACA] O: [ABCACA] X: [ABCA][C][A]
//  - ABCCBA: D: [ABCCBA] O: [A][B][CC][B][A] X: [A][B][CC][B][A]
//
// This matcher supports a few simple options:
//  - `null_data`: Use the null character instead of newline, something
//                 historically supported by tools like sed / uniq.
//  - `print_count`: Prepend output lines with the number of matches for
//                   that output line (incl. itself) that were replaced,
//                   aka filtered out due to matching with it.
//  - `print_score`: Prepend output lines with the score of the line they
//                   were matched to, or an upper-bound value, if not
//                   matched. This causes all lines to be printed. This
//                   option is mainly useful for test-runs to tune the
//                   threshold to use.
//  - `skip_marker`: A string to be printed wherever there is a gap
//                   compared to the original lines, i.e., to visually
//                   indicate skipping forward through repeating entries.
//
// Finally, this class also supports a sed-like feature preceding the
// edit distance calculation, through a regular expression and a substitution
// pattern. This uses the C++ regex library and supports its supported set
// of syntax (ECMAScript / JavaScript by default, also support basic / extended
// regular expressions, as well as sed substitutions).
//
// The substitutions do not apply to the output getting printed (if you need
// to do that, just use sed/awk, obviously), but is instead used to create
// the lines that are actually compared for edit-distance scoring. This is
// often necessary to increase the signal-to-noise ratio by removing or
// cutting down on both long and heavily repeating substrings (e.g., origin
// or date of a log entry), and rapidly changing substrings (e.g., seconds
// or nanoseconds of log timestamps). In other words, one should use this
// to extract the meaningful parts of the lines of text.
class Matcher {
 public:
  explicit Matcher(MatcherParameters params) : params_(std::move(params)) {
    // The regex should be consistent with ignore-case option.
    assert(params_.ignore_case ==
           ((params_.input_regex.flags() & std::regex_constants::icase) != 0));
  }

  // Discard pending lines and reset the matcher.
  void Reset() {
    skipped_last_ = false;
    queue_.clear();
  }

  // Add input lines to the matcher (to be filtered through).
  void AddInputLine(std::string ln);
  template <typename Range>
  void AddInputLines(Range&& lns) {
    // A bit of meta-prog to perfectly forward range elements.
    using std::begin;
    using ElemValue =
        std::remove_reference_t<decltype(*begin(std::forward<Range>(lns)))>;
    using ElemRef = std::conditional_t<std::is_rvalue_reference_v<Range&&>,
                                       ElemValue&&, const ElemValue&>;
    for (auto& ln : std::forward<Range>(lns)) {
      AddInputLine(std::string{static_cast<ElemRef>(ln)});
    }
  }

  // Consume the output lines from the matcher.
  // `ConsumeOutput` takes whatever is currently ready and should be printed.
  // `ConsumeAllOutputs` takes everything that is pending, effectively, this
  // stops / restarts the matcher since the window of pending lines is gone.
  [[nodiscard]] std::string ConsumeOutput();
  [[nodiscard]] std::string ConsumeAllOutputs();

  // Get parameters. Parameters are immutable, to change a matcher's settings,
  // just make a new one.
  [[nodiscard]] const MatcherParameters& Params() const { return params_; }

  // Simple little ref-wrapper that can be used to flush the remainder of
  // the output to as std::ostream (see `ConsumeAllOutputs`).
  struct Flusher {
    Matcher* parent;
  };
  [[nodiscard]] Flusher Flush() { return Flusher{this}; }

 private:
  MatcherParameters params_;

  struct PendingLine {
    std::string original;
    std::string replaced;
    int prior_count = 0;
    int offset_to_match = 0;
    float match_score = std::numeric_limits<float>::infinity();
  };

  std::deque<PendingLine> queue_;
  bool skipped_last_ = false;

  std::string ConsumeOutputImpl(bool flush);
};

// Streaming operators. A typical flow is:
//
//  fuzzniq::Matcher matcher;
//  while (in) {
//    in >> matcher;
//    out << matcher;
//  }
//  out << matcher.Flush();
//
std::istream& operator>>(std::istream& in, Matcher& matcher);
std::ostream& operator<<(std::ostream& out, Matcher& matcher);
std::ostream& operator<<(std::ostream& out, Matcher::Flusher matcher_flush);

}  // namespace fuzzniq

#endif  // FUZZNIQ_MATCHER_H_
