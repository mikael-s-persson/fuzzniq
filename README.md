# fuzzniq

Command-line tool for fuzzy string matching

This tool is a sort of multi-line approximate-matching version of the `uniq` tool.
The main purpose is to attempt to shorten long text logs where many mostly
repeating lines can make it difficult to narrow down to interesting sections that
break away from the common patterns.

### Building

This library uses the [Bazel](https://bazel.build/) build system. As such, to
build this library from source, use the following:

```sh
$ git clone https://github.com/mikael-s-persson/fuzzniq.git
$ cd fuzzniq
$ bazel build //fuzzniq
```

Configurations available include `--config=clang` (for Clang) and `--config=libc++` (for Clang + libc++).
This uses C++17, and thus requires a compiler from this decade.

To import this library into your own Bazel project, put the following in your MODULE.bazel:

```py
bazel_dep(name = "fuzzniq")
git_override(
    module_name = "fuzzniq",
    # Update to desired or latest commit.
    commit = "c32b33ec85d8ad080a1ee756ef2784ae3d06dbfa",
    remote = "https://github.com/mikael-s-persson/fuzzniq.git",
)

# Bazel rules should depend on: "@fuzzniq//fuzzniq" (tool) or "@fuzzniq//fuzzniq:matcher" (C++ library)
```

### Examples

Here is a very basic example (option: `-t 0.3` for considering lines will less than 30% of edits between them as matching):

```sh
echo "Server started...
Connected to client 'foo'
Connected to client 'bar'
Connected to client 'baz'
Connected to client 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)" | fuzzniq -t 0.3
Server started...
Connected to client 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)
```

Printing the number of lines being represented by a single output line (option: `-c`):

```sh
echo "Server started...
Connected to client 'foo'
Connected to client 'bar'
Connected to client 'baz'
Connected to client 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)" | fuzzniq -t 0.3 -c
      1 Server started...
      4 Connected to client 'flower'
      1 ERROR: client 'baz' disconnected
      1 Segmentation Fault (core dumped)
```

Marking skipped sections (option: `-y "...skipped..."` to provide a marker for skipped lines):

```sh
echo "Server started...
Connected to client 'foo'
Connected to client 'bar'
Connected to client 'baz'
Connected to client 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)" | fuzzniq -t 0.3 -y "...skipped..."
Server started...
...skipped...
Connected to client 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)
```

Using a longer matching window for repeating patterns (option: `-l 3` for three-line patterns):

```sh
echo "Server started...
Connected to client 'foo'
Authenticated client 'foo'
Start client session for 'foo'
Connected to client 'bar'
Authenticated client 'bar'
Start client session for 'bar'
Connected to client 'baz'
Authenticated client 'baz'
Start client session for 'baz'
Connected to client 'flower'
Authenticated client 'flower'
Start client session for 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)" | fuzzniq -t 0.3 -y "...skipped..." -l 3
Server started...
...skipped...
Connected to client 'flower'
Authenticated client 'flower'
Start client session for 'flower'
ERROR: client 'baz' disconnected
Segmentation Fault (core dumped)
```

Transforming lines in sed-like fashion before computing the edit-distance to narrow the matching
down to the meaningful parts of log messages (options: `-r` (regex), `-S` (sed substitution pattern),
other syntax options are available such as ECMAScript/Javascript, or basic/extended grep):

```sh
echo "2018-11-13T21:24:52.540582653Z: my_cloud_microservice: Server started...
2018-11-13T21:24:53.245852345Z: my_cloud_microservice: Connected to client 'foo'
2018-11-13T21:24:53.786293264Z: my_cloud_microservice: Connected to client 'bar'
2018-11-13T21:24:54.567231985Z: my_cloud_microservice: Connected to client 'baz'
2018-11-13T21:24:55.154874245Z: my_cloud_microservice: Connected to client 'flower'
2018-11-13T21:24:56.543287534Z: my_cloud_microservice: ERROR: client 'baz' disconnected
2018-11-13T21:24:56.554796170Z: my_cloud_microservice: Segmentation Fault (core dumped)" | bazel-bin/fuzzniq/fuzzniq -t 0.4 -r ".*my_cloud_microservice:(.*)" -S "\1"
2018-11-13T21:24:52.540582653Z: my_cloud_microservice: Server started...
2018-11-13T21:24:55.154874245Z: my_cloud_microservice: Connected to client 'flower'
2018-11-13T21:24:56.543287534Z: my_cloud_microservice: ERROR: client 'baz' disconnected
2018-11-13T21:24:56.554796170Z: my_cloud_microservice: Segmentation Fault (core dumped)
```

### Details

The fuzzy matching (or approximate string matching) uses one of a few
possible edit-distance metrics (with `-m,--method` option) and a
configurable threshold (with `-t,--threshold FLOAT` option) to classify
two lines as being a good-enough match.

With a window of 1 line, this tool is essentially a fuzzy `uniq` tool,
filtering out approximately-duplicated consecutive lines.

With longer windows (with `-l,--lines INT` option), things get a bit
trickier (and more useful). The general idea is to filter out
approximately-repeating patterns. For example, text logs that contain
long sections of mundane and repetitive entries that vary slightly,
not just in terms of "edits" but also in the ordering or permutations
of the repeating entries.

There are three options for multi-line pattern matching. First, by default,
any line that matches a prior line within the window causes that prior
line to be filtered out. This implies that any ordering or sequencing of
the matching lines is ignored, the mere fact that one exists is sufficient.

Second, one can enable `-O,--ordered`, which additionally requires
matches to be "in-order", meaning that the prior line to which a new
line is matching should not precede a line that has already been matched
(i.e. filtered out).

Finally, one can enable `-X,--sequenced` which requires that matching
pairs form a repeating sequence.

Consider the following examples, using matching letters to represent
matching lines, and D, O, and X, for default, ordered and sequenced
matches, for a window of 3 lines (`-l 3`), and using `[]` to mark a
contiguous group of matching multi-line pattern (meaning that, of
that group, only the last occurrence of each "unique" line will be
printed to the output):
 - `ABCABC`: D: `[ABCABC]` O: `[ABCABC]` X: `[ABCABC]`
 - `ABCAAA`: D: `[ABCAAA]` O: `[ABCA][AA]` X: `[ABCA][AA]`
 - `ABCACA`: D: `[ABCACA]` O: `[ABCACA]` X: `[ABCA][C][A]`
 - `ABCCBA`: D: `[ABCCBA]` O: `[A][B][CC][B][A]` X: `[A][B][CC][B][A]`

This tool supports a few basic options:
 - `-i,--ignore-case`: Ignore case, duh.
 - `-z,--null-data`: Use the null character instead of newline, something
                     historically supported by tools like `sed` / `uniq`.
 - `-c,--count`: Prepend output lines with the number of matches for
                 that output line (incl. itself) that were replaced,
                 aka filtered out due to matching with it.
 - `-d,--distance`: Prepend output lines with the edit-distance to the
                    line they were matched to, or an upper-bound value,
                    if not matched. This causes all lines to be printed.
                    This option is mainly useful for test-runs to tune the
                    threshold to use.
 - `-y,--skip-marker TEXT`: A string to be printed wherever there is a gap
                            compared to the original lines, i.e., to visually
                            indicate skipping forward through repeating entries.

Finally, this tool also supports a sed-like feature preceding the
edit distance calculation, through a regular expression and a substitution
pattern. This uses the C++ regex library and supports its supported set
of syntax (ECMAScript / JavaScript by default, also basic / extended
regular expressions, and sed substitution patterns).

The substitutions do not apply to the output getting printed (if you need
to do that, just use sed/awk, obviously), but is instead used to create
the lines that are actually compared with edit-distance scoring and
thresholding. This is often necessary to increase the signal-to-noise
ratio by removing or cutting down on both long and heavily repeating
sub-strings (e.g., origin or date on a log entry), and meaninglessly changing
sub-strings (e.g., nanoseconds of log timestamps). In other words, one should
use this to extract the meaningful parts of the lines of text. One can
also use the `-n,--matches-only` option to only compute edit-distance between
lines that match the regex (i.e., non-matching lines are always "unique").

### Reporting issues

This is very experimental. There are bugs in this tool and library, I am sure of it.

Please report any issues at: https://github.com/mikael-s-persson/fuzzniq/issues

### Development

https://github.com/mikael-s-persson/fuzzniq

### Contributing

Contribute through the typical github mechanisms:

 - Report issues
 - Create pull requests

### License

Copyright 2025-present, Mikael Persson.
This project licensed under the MIT license.
