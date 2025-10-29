
#include <cerrno>
#include <cstring>
#include <fstream>
#include <regex>

#include "CLI/CLI.hpp"
#include "fuzzniq/matcher.h"

int main(int argc, char** argv) {
  CLI::App cli_fz{"Fuzzy filtering of lines of text"};

  std::string arg_input_path = "stdin";
  cli_fz.add_option("input", arg_input_path, "input path, default: stdin")
      ->transform(CLI::EscapedString)
      ->check(CLI::ExistingFile);

  std::string arg_output_path = "stdout";
  cli_fz
      .add_option("-o,--output", arg_output_path,
                  "output path, default: stdout")
      ->transform(CLI::EscapedString);

  int arg_line_count = 1;
  cli_fz.add_option("-l,--lines", arg_line_count,
                    "number of lines in matching patterns, default: 1");

  float arg_threshold = 0.1F;
  cli_fz.add_option(
      "-t,--threshold", arg_threshold,
      "threshold of distance to consider lines to match, default: 0.1");

  std::string arg_regex_pattern;
  auto* opt_regex_pattern =
      cli_fz.add_option("-r,--regexp", arg_regex_pattern,
                        "regular expression matching pattern, default is all");

  std::string arg_basic_regex_pattern;
  auto* opt_basic_regex = cli_fz.add_flag(
      "-G,--basic-regexp", arg_basic_regex_pattern,
      "basic regular expression matching pattern, default is all");

  std::string arg_extended_regex_pattern;
  auto* opt_extended_regex = cli_fz.add_flag(
      "-E,--extended-regexp", arg_extended_regex_pattern,
      "extended regular expression matching pattern, default is all");
  opt_extended_regex->excludes(opt_regex_pattern);
  opt_extended_regex->excludes(opt_basic_regex);
  opt_basic_regex->excludes(opt_regex_pattern);
  opt_basic_regex->excludes(opt_extended_regex);

  std::string arg_ecma_substitution;
  auto* opt_ecma_substitution = cli_fz.add_option(
      "-s,--sub", arg_ecma_substitution,
      "regex substitution pattern (ECMAScript), default: all");

  std::string arg_sed_substitution;
  auto* opt_sed_substitution = cli_fz.add_option(
      "-S,--sed", arg_sed_substitution,
      "regex substitution pattern (sed format), default: all");
  opt_sed_substitution->excludes(opt_ecma_substitution);
  opt_ecma_substitution->excludes(opt_sed_substitution);

  bool arg_ignore_case = false;
  cli_fz.add_flag("-i,--ignore-case", arg_ignore_case,
                  "ignore case distinctions in patterns and data, default is "
                  "case sensitive");

  bool arg_strict_order = false;
  cli_fz.add_flag(
      "-O,--ordered", arg_strict_order,
      "require multiline matches to be ordered, default is false.\n"
      "For example, with 3-line window, without ordering, 'ABBAC'\n"
      "would group together 'ABBA' since all those elements have a\n"
      "match within 3 predecessors. With ordering enabled, only 'BB'\n"
      "would be grouped together since the 'A' matches are not in\n"
      "an order that is consistent with the 'B' matches.");

  bool arg_strict_seq = false;
  cli_fz.add_flag(
      "-X,--sequenced", arg_strict_seq,
      "require multiline matches to be sequenced, default is false.\n"
      "For example, with 3-line window, without sequencing, 'ABCAC'\n"
      "would all be group together since all those elements have a\n"
      "match within 3 predecessors. With sequencing enabled, only 'ABCA'\n"
      "would be grouped together since the 'C' breaks the repetition\n"
      "of 'ABC' established prior. For efficiency, there is no attempt\n"
      "to find for the longest repeating sequences.");

  std::string arg_method_str = "normalized_levenshtein";
  cli_fz
      .add_option(
          "-m,--method", arg_method_str,
          "edit distance calculation method, default is normalized_levenshtein")
      ->check(CLI::IsMember({"normalized_levenshtein", "levenshtein",
                             "normalized_hamming", "hamming"}));

  bool arg_print_count = false;
  cli_fz.add_flag("-c,--count", arg_print_count,
                  "prefix output lines by the number of prior occurrences");

  bool arg_print_score = false;
  cli_fz.add_flag(
      "-d,--distance", arg_print_score,
      "prefix output lines by the edit distance to best matching line.\n"
      "Note, this causes all lines to be printed, no filtering out.\n"
      "Moreover, edit distances will have an upper-bound value if there\n"
      "is no candidate to match against. This option is mostly for test\n"
      "runs and tuning the threshold. Generally, setting a very high\n"
      "threshold will help get more useful outputs.");

  bool arg_no_copy = false;
  cli_fz.add_flag("-n,--matches-only", arg_no_copy,
                  "only compare lines that match the regular expression, "
                  "similar to '-n' and 's/a/b/p' in sed");

  bool arg_null_data = false;
  cli_fz.add_flag("-z,--null-data", arg_null_data,
                  "data lines end in 0 byte, not newline");

  std::string arg_skip_marker;
  cli_fz.add_option("-y,--skip-marker", arg_skip_marker,
                    "line to print to mark skip points, default: none");

  try {
    cli_fz.parse(argc, argv);
  } catch (const CLI::ParseError& e) {
    return cli_fz.exit(e);
  }

  std::ifstream in_file;
  std::istream* in_ptr = nullptr;
  if (arg_input_path != "stdin") {
    in_file.open(arg_input_path.c_str());
    if (!in_file.is_open()) {
      std::cerr << "Failed to open input file '" << arg_input_path
                << "', got: " << std::strerror(errno)
                << std::endl;  // NOLINT(performance-avoid-endl)
      return 1;
    }
    in_ptr = &in_file;
  } else {
    in_ptr = &std::cin;
  }

  std::ofstream out_file;
  std::ostream* out_ptr = nullptr;
  if (arg_output_path != "stdout") {
    out_file.open(arg_output_path.c_str());
    if (!out_file.is_open()) {
      std::cerr << "Failed to open output file '" << arg_output_path
                << "', got: " << std::strerror(errno)
                << std::endl;  // NOLINT(performance-avoid-endl)
      return 2;
    }
    out_ptr = &out_file;
  } else {
    out_ptr = &std::cout;
  }

  if (arg_line_count <= 0) {
    std::cerr << "Line count must be positive! Got " << arg_line_count
              << std::endl;  // NOLINT(performance-avoid-endl)
    return 3;
  }

  if (arg_threshold <= 0.0F) {
    std::cerr << "Threshold must be positive! Got " << arg_threshold
              << std::endl;  // NOLINT(performance-avoid-endl)
    return 4;
  }

  if ((!arg_sed_substitution.empty() || !arg_ecma_substitution.empty()) &&
      (arg_regex_pattern.empty() && arg_basic_regex_pattern.empty() &&
       arg_extended_regex_pattern.empty())) {
    std::cerr << "Substitution format string provide without a regex matching "
                 "pattern!"
              << std::endl;  // NOLINT(performance-avoid-endl)
    return 5;
  }

  fuzzniq::MatcherParameters params;
  params.line_count = arg_line_count;
  params.threshold = arg_threshold;
  params.ignore_case = arg_ignore_case;
  params.null_data = arg_null_data;
  params.print_count = arg_print_count;
  params.print_score = arg_print_score;
  params.strict_order = arg_strict_order;
  params.strict_seq = arg_strict_seq;
  params.skip_marker = arg_skip_marker;

  std::string* regex_str = nullptr;
  std::regex_constants::syntax_option_type syntax_opt =
      std::regex_constants::optimize;
  if (!arg_basic_regex_pattern.empty()) {
    syntax_opt = syntax_opt | std::regex_constants::basic;
    regex_str = &arg_basic_regex_pattern;
  } else if (!arg_extended_regex_pattern.empty()) {
    syntax_opt = syntax_opt | std::regex_constants::extended;
    regex_str = &arg_extended_regex_pattern;
  } else {
    syntax_opt = syntax_opt | std::regex_constants::ECMAScript;
    if (arg_regex_pattern.empty()) {
      arg_regex_pattern = "(.*)";
    }
    regex_str = &arg_regex_pattern;
  }
  if (arg_ignore_case) {
    syntax_opt = syntax_opt | std::regex_constants::icase;
  }
  try {
    params.input_regex.assign(*regex_str, syntax_opt);
  } catch (const std::regex_error& e) {
    std::cerr << "Invalid regex pattern! Got: " << e.what()
              << std::endl;  // NOLINT(performance-avoid-endl)
    return 6;
  }

  if (!arg_sed_substitution.empty()) {
    params.input_regex_fmt = arg_sed_substitution;
    params.input_regex_flags = std::regex_constants::format_sed;
  } else if (!arg_ecma_substitution.empty()) {
    params.input_regex_fmt = arg_ecma_substitution;
  }
  if (arg_no_copy) {
    params.input_regex_flags =
        params.input_regex_flags | std::regex_constants::format_no_copy;
  }

  for (auto [m_name, m_enum] :
       {std::pair{"normalized_levenshtein",
                  fuzzniq::Method::kNormalizedLevenshtein},
        std::pair{"levenshtein", fuzzniq::Method::kLevenshtein},
        std::pair{"normalized_hamming", fuzzniq::Method::kNormalizedHamming},
        std::pair{"hamming", fuzzniq::Method::kHamming}}) {
    if (arg_method_str == m_name) {
      params.method = m_enum;
      break;
    }
  }

  fuzzniq::Matcher matcher{std::move(params)};
  while (*in_ptr) {
    (*in_ptr) >> matcher;
    (*out_ptr) << matcher;
  }
  (*out_ptr) << matcher.Flush();

  return 0;
}
