#include "fuzzniq/matcher.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <regex>
#include <string>
#include <vector>

#include "fmt/format.h"

namespace fuzzniq {

namespace {

constexpr bool kDebugPrint = false;
constexpr bool kDebugPrintSteps = false;

float ComputeLevenshteinDistance(const std::string& s, const std::string& t) {
  // create two work vectors of integer distances
  std::vector<int> v0(t.size() + 1, 0);
  std::vector<int> v1(t.size() + 1, 0);

  // initialize v0 (the previous row of distances)
  // this row is A[0][i]: edit distance from an empty s to t;
  // that distance is the number of characters to append to s to make t.
  for (int i = 0; i < t.size() + 1; ++i) {
    v0[i] = i;
  }

  for (int i = 0; i < s.size(); ++i) {
    // calculate v1 (current row distances) from the previous row v0

    // first element of v1 is A[i + 1][0]
    //   edit distance is delete (i + 1) chars from s to match empty t
    v1[0] = i + 1;

    // use formula to fill in the rest of the row
    for (int j = 0; j < t.size(); ++j) {
      // calculating costs for A[i + 1][j + 1]
      int deletion_cost = v0[j + 1] + 1;
      int insertion_cost = v1[j] + 1;
      int substitution_cost = 0;
      if (s[i] == t[j]) {
        substitution_cost = v0[j];
      } else {
        substitution_cost = v0[j] + 1;
      }

      v1[j + 1] = std::min({deletion_cost, insertion_cost, substitution_cost});
    }

    // copy v1 (current row) to v0 (previous row) for next iteration
    // since data in v1 is always invalidated, a swap without copy could be more
    // efficient
    v0.swap(v1);
  }
  // after the last swap, the results of v1 are now in v0
  return static_cast<float>(v0[t.size()]);
}

float ComputeNormalizedLevenshteinDistance(const std::string& s,
                                           const std::string& t) {
  return ComputeLevenshteinDistance(s, t) /
         static_cast<float>(std::max(s.size(), t.size()));
}

float ComputeHammingDistance(const std::string& s, const std::string& t) {
  const auto len = std::min(s.size(), t.size());
  int dist_counter = std::abs(static_cast<int>(s.size() - t.size()));
  for (int i = 0; i < len; ++i) {
    if (s[i] != t[i]) {
      ++dist_counter;
    }
  }
  return static_cast<float>(dist_counter);
}

float ComputeNormalizedHammingDistance(const std::string& s,
                                       const std::string& t) {
  return ComputeHammingDistance(s, t) /
         static_cast<float>(std::max(s.size(), t.size()));
}

float ComputeEditDistance(const std::string& s, const std::string& t,
                          Method method) {
  if (s.empty() || t.empty()) {
    return std::numeric_limits<float>::infinity();
  }
  switch (method) {
    case Method::kNormalizedLevenshtein:
      return ComputeNormalizedLevenshteinDistance(s, t);
    case Method::kLevenshtein:
      return ComputeLevenshteinDistance(s, t);
    case Method::kNormalizedHamming:
      return ComputeNormalizedHammingDistance(s, t);
    case Method::kHamming:
      return ComputeHammingDistance(s, t);
  }
  return std::numeric_limits<float>::infinity();
}

}  // namespace

void Matcher::AddInputLine(std::string ln) {
  std::string ln_out;
  if (params_.input_regex_fmt.empty()) {
    if (std::regex_match(ln, params_.input_regex)) {
      ln_out = ln;
    }
  } else {
    try {
      ln_out =
          std::regex_replace(ln, params_.input_regex, params_.input_regex_fmt,
                             params_.input_regex_flags);
    } catch (const std::regex_error& e) {
      std::cerr << "REGEX ERROR! Got: " << e.what()
                << std::endl;  // NOLINT(performance-avoid-endl)
    }
  }
  int prior_count = 0;
  int offset_to_match = 1;
  for (auto it = queue_.rbegin(), it_end = queue_.rend(); it != it_end;
       ++it, ++offset_to_match) {
    if (offset_to_match > params_.line_count ||
        (params_.strict_order && it->offset_to_match > 0)) {
      break;
    }
    if (it->offset_to_match == 0 &&
        ComputeEditDistance(ln_out, it->replaced, params_.method) <=
            params_.threshold) {
      prior_count = it->prior_count + 1;
      it->offset_to_match = offset_to_match;
      break;
    }
  }

  queue_.emplace_back(PendingLine{.original = std::move(ln),
                                  .replaced = std::move(ln_out),
                                  .prior_count = prior_count});

  if constexpr (kDebugPrintSteps) {
    fmt::print(">>>>>>>>\n");
    for (const auto& p : queue_) {
      fmt::print("{} ({}) {: 5d} {: 5d}\n", p.original, p.replaced,
                 p.prior_count, p.offset_to_match);
    }
    fmt::print("========\n");
  }

  if (queue_.size() > 2 * params_.line_count &&
      queue_[params_.line_count].prior_count != 0 &&
      queue_[params_.line_count].offset_to_match == 0 &&
      queue_[params_.line_count + 1].offset_to_match > 0) {
    // Negate future matches, pattern has been broken here.

    int p_beg = params_.line_count - 1;
    for (; p_beg > 0; --p_beg) {
      if (queue_[p_beg].prior_count != queue_[params_.line_count].prior_count) {
        break;
      }
    }
    ++p_beg;
    for (int i = p_beg;
         i < p_beg + params_.line_count &&
         queue_[i].prior_count == queue_[params_.line_count].prior_count;
         ++i) {
      for (int j = i; queue_[j].offset_to_match > 0;) {
        j += queue_[j].offset_to_match;
        queue_[j].prior_count -= queue_[i].prior_count + 1;
      }
      queue_[i].offset_to_match = 0;
    }
  }

  if constexpr (kDebugPrintSteps) {
    for (const auto& p : queue_) {
      fmt::print("{} ({}) {: 5d} {: 5d}\n", p.original, p.replaced,
                 p.prior_count, p.offset_to_match);
    }
    fmt::print("<<<<<<<<\n");
  }
}

std::string Matcher::GetOutputLineImpl(bool flush) {
  while (
      ((flush && !queue_.empty()) || queue_.size() > 2 * params_.line_count)) {
    std::string ln;
    if ((queue_.front().offset_to_match == 0) || kDebugPrint) {
      if (params_.print_count) {
        fmt::format_to(std::back_inserter(ln), " {: 6d} ",
                       queue_.front().prior_count + 1);
      }
      ln.append(std::move(queue_.front().original));
      if constexpr (kDebugPrint) {
        fmt::format_to(std::back_inserter(ln), "   {} {} {: 5d} {: 5d}",
                       (queue_.front().offset_to_match == 0 ? "<--" : "-->"),
                       queue_.front().replaced, queue_.front().prior_count,
                       queue_.front().offset_to_match);
      }
      ln.push_back(params_.null_data ? '\0' : '\n');
    }
    queue_.pop_front();
    if (ln.empty() && flush) {
      continue;
    }
    return ln;
  }
  return {};
}

std::string Matcher::GetOutputLine() {
  return GetOutputLineImpl(/*flush=*/false);
}

std::string Matcher::Flusher::GetOutputLine() const {
  return parent->GetOutputLineImpl(/*flush=*/true);
}

std::istream& operator>>(std::istream& in, Matcher& matcher) {
  std::string ln;
  std::getline(in, ln, matcher.params_.null_data ? '\0' : '\n');
  if (in) {
    matcher.AddInputLine(std::move(ln));
  }
  return in;
}

std::ostream& operator<<(std::ostream& out, Matcher& matcher) {
  std::string ln_out = matcher.GetOutputLine();
  while (!ln_out.empty()) {
    out << ln_out;
    ln_out = matcher.GetOutputLine();
  }
  return out;
}

std::ostream& operator<<(std::ostream& out, Matcher::Flusher matcher_flush) {
  std::string ln_out = matcher_flush.GetOutputLine();
  while (!ln_out.empty()) {
    out << ln_out;
    ln_out = matcher_flush.GetOutputLine();
  }
  return out << std::flush;
}

}  // namespace fuzzniq
