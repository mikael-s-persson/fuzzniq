#include "fuzzniq/matcher.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
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

float MaxEditDistance(const std::string& s, Method method) {
  switch (method) {
    case Method::kNormalizedLevenshtein:
      [[fallthrough]];
    case Method::kNormalizedHamming:
      return 1.0F;
    case Method::kLevenshtein:
      [[fallthrough]];
    case Method::kHamming:
      return static_cast<float>(s.size());
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

  float best_score = MaxEditDistance(ln_out, params_.method);
  int best_match = queue_.size();
  int earliest_match =
      std::max(0, static_cast<int>(queue_.size()) - params_.line_count);
  int in_seq_match = earliest_match;
  int new_seq_start = in_seq_match;
  if (params_.strict_seq || params_.strict_order) {
    // Find last matched entry.
    for (int i = std::max(0, in_seq_match - params_.line_count);
         i < queue_.size(); ++i) {
      if (queue_[i].offset_to_match > 0) {
        // Earliest match needs to be next, at least.
        in_seq_match = i + 1;
        new_seq_start = i + queue_[i].offset_to_match + 1;
      }
    }
    if (new_seq_start > earliest_match && new_seq_start < queue_.size()) {
      in_seq_match = new_seq_start;
    }
  }
  // Find first matched entry.
  for (int i = in_seq_match; i < queue_.size(); ++i) {
    if (queue_[i].offset_to_match > 0) {
      continue;
    }
    // Check for match.
    const float score =
        ComputeEditDistance(ln_out, queue_[i].replaced, params_.method);
    best_score = std::min(score, best_score);
    if (score <= params_.threshold) {
      best_match = i;
      break;
    }
    if (params_.strict_seq && new_seq_start > i) {
      // Skip ahead to new sequence start.
      i = new_seq_start - 1;
    }
  }

  // If we have not used the earliest match (to preserve sequence), we use
  // the best match.
  int prior_count = 0;
  if (best_match < queue_.size()) {
    prior_count = queue_[best_match].prior_count + 1;
    queue_[best_match].offset_to_match = queue_.size() - best_match;
  }

  queue_.emplace_back(PendingLine{.original = std::move(ln),
                                  .replaced = std::move(ln_out),
                                  .prior_count = prior_count,
                                  .offset_to_match = 0,
                                  .match_score = best_score});

  if constexpr (kDebugPrintSteps) {
    fmt::print(">>>>>>>>\n");
    for (const auto& p : queue_) {
      fmt::print("{} ({}) {: 5d} {: 5d} {: 3.3f}\n", p.original, p.replaced,
                 p.prior_count, p.offset_to_match, p.match_score);
    }
    fmt::print("<<<<<<<<\n");
  }
}

std::string Matcher::ConsumeOutputImpl(bool flush) {
  std::string ln;
  while (
      ((flush && !queue_.empty()) || queue_.size() > 2 * params_.line_count)) {
    if ((queue_.front().offset_to_match == 0) || params_.print_score ||
        kDebugPrint) {
      if (skipped_last_ && !params_.skip_marker.empty()) {
        ln.append(params_.skip_marker);
        ln.push_back(params_.null_data ? '\0' : '\n');
      }
      skipped_last_ = false;
      if (params_.print_count) {
        fmt::format_to(std::back_inserter(ln), " {: 6d} ",
                       (queue_.front().offset_to_match > 0
                            ? 0
                            : queue_.front().prior_count + 1));
      }
      if (params_.print_score) {
        fmt::format_to(std::back_inserter(ln), " {: 3.3f} ",
                       queue_.front().match_score);
      }
      ln.append(std::move(queue_.front().original));
      if constexpr (kDebugPrint) {
        fmt::format_to(
            std::back_inserter(ln), "   {} {} {: 5d} {: 5d} {: 3.3f}",
            (queue_.front().offset_to_match == 0 ? "<--" : "-->"),
            queue_.front().replaced, queue_.front().prior_count,
            queue_.front().offset_to_match, queue_.front().match_score);
      }
      ln.push_back(params_.null_data ? '\0' : '\n');
    }
    if (queue_.front().offset_to_match != 0) {
      skipped_last_ = true;
    }
    queue_.pop_front();
  }
  return ln;
}

std::string Matcher::ConsumeOutput() {
  return ConsumeOutputImpl(/*flush=*/false);
}

std::string Matcher::ConsumeAllOutputs() {
  return ConsumeOutputImpl(/*flush=*/true);
}

std::istream& operator>>(std::istream& in, Matcher& matcher) {
  std::string ln;
  std::getline(in, ln, matcher.Params().null_data ? '\0' : '\n');
  if (in) {
    matcher.AddInputLine(std::move(ln));
  }
  return in;
}

std::ostream& operator<<(std::ostream& out, Matcher& matcher) {
  return out << matcher.ConsumeOutput();
}

std::ostream& operator<<(std::ostream& out, Matcher::Flusher matcher_flush) {
  return out << matcher_flush.parent->ConsumeAllOutputs() << std::flush;
}

}  // namespace fuzzniq
