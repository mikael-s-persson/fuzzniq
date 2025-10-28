#ifndef FUZZNIQ_MATCHER_H_
#define FUZZNIQ_MATCHER_H_

#include <cstdint>
#include <deque>
#include <iostream>
#include <regex>
#include <string>

namespace fuzzniq {

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
  Method method = Method::kNormalizedLevenshtein;
  float threshold = 0.1F;
  int line_count = 1;
  bool print_count = false;
  bool null_data = false;
  bool strict_order = false;
};

class Matcher {
 public:
  explicit Matcher(MatcherParameters params) : params_(std::move(params)) {}

  void AddInputLine(std::string ln);
  [[nodiscard]] std::string GetOutputLine();

  struct Flusher {
    Matcher* parent;
    [[nodiscard]] std::string GetOutputLine() const;
  };
  [[nodiscard]] Flusher Flush() { return Flusher{this}; }

  friend std::istream& operator>>(std::istream& in, Matcher& matcher);
  friend std::ostream& operator<<(std::ostream& out, Matcher& matcher);

 private:
  MatcherParameters params_;

  struct PendingLine {
    std::string original;
    std::string replaced;
    int prior_count = 0;
    int offset_to_match = 0;
  };

  std::deque<PendingLine> queue_;

  std::string GetOutputLineImpl(bool flush);
};

std::istream& operator>>(std::istream& in, Matcher& matcher);
std::ostream& operator<<(std::ostream& out, Matcher& matcher);
std::ostream& operator<<(std::ostream& out, Matcher::Flusher matcher_flush);

}  // namespace fuzzniq

#endif  // FUZZNIQ_MATCHER_H_
