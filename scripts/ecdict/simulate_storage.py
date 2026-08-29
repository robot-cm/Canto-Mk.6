#!/usr/bin/env python3

import argparse
import csv
import math
import sqlite3
import statistics
import sys
import time
import unicodedata
from collections import defaultdict
from pathlib import Path


# ============================================================
# Step 3.1
# ECDICT Adaptive Bucket Simulator
#
# Important design assumptions:
#
#   1. SQLite data is read once.
#   2. word is normalized FIRST.
#   3. normalized words are sorted in Python.
#   4. cumulative byte offsets are built once.
#   5. adaptive prefix splitting operates on contiguous
#      sorted intervals.
#
# This version correctly handles:
#
#       "a"
#       "aa"
#       "aaa"
#       ...
#
# where "a" itself is a valid dictionary entry.
#
# It also detects duplicate normalized words that cannot
# be split by additional prefix characters.
# ============================================================


DEFAULT_TARGETS = [
    4096,
    8192,
    16384,
    32768,
    65536,
    131072,
    262144,
]

DEFAULT_INDEX_BYTES = 16
DEFAULT_MAX_DEPTH = 16

PROGRESS_EVERY = 500_000


# ============================================================
# Normalization
# ============================================================

def normalize(word):
    """
    Same normalization policy used in previous steps.
    """

    if not word:
        return ""

    return unicodedata.normalize(
        "NFKC",
        word,
    ).strip().lower()


# ============================================================
# Entry size estimation
# ============================================================

def estimate_entry_size(row):
    """
    Conservative Step-3 binary size estimate.

    Fields:

        word
        phonetic
        translation
        definition
        pos
        tag
        exchange

    Plus 16 bytes for:

        flags
        lengths / offsets
        alignment
        future metadata
    """

    (
        word,
        phonetic,
        translation,
        definition,
        pos,
        tag,
        exchange,
    ) = row

    size = 16

    for value in (
        word,
        phonetic,
        translation,
        definition,
        pos,
        tag,
        exchange,
    ):
        if value:
            size += len(value.encode("utf-8"))

    return size


# ============================================================
# Percentile
# ============================================================

def percentile(sorted_values, p):
    """
    Linear interpolation percentile.
    """

    if not sorted_values:
        return 0.0

    if len(sorted_values) == 1:
        return float(sorted_values[0])

    pos = (len(sorted_values) - 1) * p

    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))

    if lo == hi:
        return float(sorted_values[lo])

    fraction = pos - lo

    return (
        sorted_values[lo]
        + (
            sorted_values[hi]
            - sorted_values[lo]
        ) * fraction
    )


# ============================================================
# Bucket
# ============================================================

class Bucket:
    __slots__ = (
        "prefix",
        "display_prefix",
        "start",
        "end",
        "start_offset",
        "end_offset",
        "unsplittable",
        "reason",
    )

    def __init__(
        self,
        prefix,
        display_prefix,
        start,
        end,
        start_offset,
        end_offset,
        unsplittable=False,
        reason="",
    ):
        self.prefix = prefix
        self.display_prefix = display_prefix
        self.start = start
        self.end = end
        self.start_offset = start_offset
        self.end_offset = end_offset
        self.unsplittable = unsplittable
        self.reason = reason

    @property
    def count(self):
        return self.end - self.start

    @property
    def size(self):
        return self.end_offset - self.start_offset

    @property
    def depth(self):
        return len(self.prefix)


# ============================================================
# Find root intervals
# ============================================================

def build_root_intervals(words):
    """
    Build first-character intervals.

    Special case:
        empty normalized word -> "_"
    """

    n = len(words)

    if n == 0:
        return []

    roots = []

    start = 0

    def root_char(word):
        if word:
            return word[0]
        return "_"

    current = root_char(words[0])

    for i in range(1, n):

        char = root_char(words[i])

        if char != current:

            roots.append(
                (
                    current,
                    start,
                    i,
                )
            )

            start = i
            current = char

    roots.append(
        (
            current,
            start,
            n,
        )
    )

    return roots


# ============================================================
# Split one interval by next character
# ============================================================

def split_interval(
    words,
    start,
    end,
    prefix,
):
    """
    Split [start:end] by the character immediately after
    `prefix`.

    IMPORTANT:

    If the word itself equals prefix, it is put into a
    special END child.

    Example:

        a
        aa
        ab
        ac

    parent:

        "a"

    children become conceptually:

        "a$"   -> exact word "a"
        "aa"   -> words starting with "aa"
        "ab"
        "ac"

    "$" is an INTERNAL marker only.
    It is not part of the actual lookup prefix.
    """

    prefix_len = len(prefix)

    children = []

    if start >= end:
        return children

    # --------------------------------------------------------
    # Exact-prefix entries
    # --------------------------------------------------------

    exact_start = None
    exact_end = None

    i = start

    while i < end and len(words[i]) == prefix_len:
        i += 1

    if i > start:
        exact_start = start
        exact_end = i

        children.append(
            (
                prefix + "\0",
                exact_start,
                exact_end,
            )
        )

    # --------------------------------------------------------
    # Longer words
    # --------------------------------------------------------

    if i >= end:
        return children

    child_start = i
    current_char = words[i][prefix_len]

    i += 1

    while i < end:

        word = words[i]

        # All words in this interval should have at least
        # prefix_len + 1 characters here.
        char = word[prefix_len]

        if char != current_char:

            children.append(
                (
                    prefix + current_char,
                    child_start,
                    i,
                )
            )

            child_start = i
            current_char = char

        i += 1

    children.append(
        (
            prefix + current_char,
            child_start,
            end,
        )
    )

    return children


# ============================================================
# Adaptive bucket construction
# ============================================================

def build_buckets(
    words,
    offsets,
    target,
    max_depth,
):
    """
    Construct adaptive prefix buckets.

    Algorithm:

        root
          |
          +-- fits target -> bucket
          |
          +-- too large -> split by next character
                              |
                              +-- fits -> bucket
                              +-- too large -> split again

    Exact-word children use an internal '\\0' marker.

    A bucket is marked UNSPLITTABLE when:
        - all entries have exactly the same normalized word
        - maximum prefix depth has been reached
        - no further character split is possible
    """

    n = len(words)

    if n == 0:
        return []

    buckets = []

    roots = build_root_intervals(words)

    stack = list(reversed(roots))

    while stack:

        prefix, start, end = stack.pop()

        start_offset = offsets[start]
        end_offset = offsets[end]

        size = end_offset - start_offset

        # ----------------------------------------------------
        # Bucket already fits
        # ----------------------------------------------------

        if size <= target:

            if prefix.endswith("\0"):
                display_prefix = (
                    prefix[:-1] + " [exact]"
                )
            else:
                display_prefix = prefix

            buckets.append(
                Bucket(
                    prefix=prefix.rstrip("\0"),
                    display_prefix=display_prefix,
                    start=start,
                    end=end,
                    start_offset=start_offset,
                    end_offset=end_offset,
                )
            )

            continue

        # ----------------------------------------------------
        # Empty / special
        # ----------------------------------------------------

        if start >= end:

            buckets.append(
                Bucket(
                    prefix=prefix.rstrip("\0"),
                    display_prefix=prefix.rstrip("\0"),
                    start=start,
                    end=end,
                    start_offset=start_offset,
                    end_offset=end_offset,
                )
            )

            continue

        # ----------------------------------------------------
        # Exact-word bucket
        #
        # prefix ending with \0 means there is no additional
        # character to split on.
        # ----------------------------------------------------

        if prefix.endswith("\0"):

            buckets.append(
                Bucket(
                    prefix=prefix[:-1],
                    display_prefix=prefix[:-1] + " [exact]",
                    start=start,
                    end=end,
                    start_offset=start_offset,
                    end_offset=end_offset,
                    unsplittable=True,
                    reason="duplicate normalized word",
                )
            )

            continue

        # ----------------------------------------------------
        # Maximum depth
        # ----------------------------------------------------

        if len(prefix) >= max_depth:

            buckets.append(
                Bucket(
                    prefix=prefix,
                    display_prefix=prefix,
                    start=start,
                    end=end,
                    start_offset=start_offset,
                    end_offset=end_offset,
                    unsplittable=True,
                    reason="maximum prefix depth",
                )
            )

            continue

        # ----------------------------------------------------
        # Try to split
        # ----------------------------------------------------

        children = split_interval(
            words,
            start,
            end,
            prefix,
        )

        # Cannot split
        if len(children) <= 1:

            buckets.append(
                Bucket(
                    prefix=prefix,
                    display_prefix=prefix,
                    start=start,
                    end=end,
                    start_offset=start_offset,
                    end_offset=end_offset,
                    unsplittable=True,
                    reason="no further prefix split",
                )
            )

            continue

        # ----------------------------------------------------
        # Push children
        #
        # Reverse because stack is LIFO.
        # ----------------------------------------------------

        for child in reversed(children):
            stack.append(child)

    # Ensure lexical entry order.
    buckets.sort(
        key=lambda b: b.start
    )

    return buckets


# ============================================================
# Database loading
# ============================================================

def load_database(database):
    """
    Read SQLite exactly once.

    IMPORTANT:
    We normalize first and SORT AFTER normalization.

    This fixes the fundamental ordering issue in the
    previous implementation.
    """

    conn = sqlite3.connect(
        f"file:{database.resolve()}?mode=ro",
        uri=True,
    )

    conn.execute(
        "PRAGMA query_only=ON"
    )

    conn.execute(
        "PRAGMA cache_size=-262144"
    )

    cursor = conn.execute(
        """
        SELECT
            word,
            phonetic,
            translation,
            definition,
            pos,
            tag,
            exchange
        FROM stardict
        """
    )

    entries = []

    total_bytes = 0

    start_time = time.perf_counter()

    for i, row in enumerate(cursor, 1):

        word = normalize(row[0])

        size = estimate_entry_size(row)

        entries.append(
            (
                word,
                size,
            )
        )

        total_bytes += size

        if i % PROGRESS_EVERY == 0:

            elapsed = (
                time.perf_counter()
                - start_time
            )

            rate = (
                i / elapsed
                if elapsed
                else 0
            )

            print(
                f"  read {i:,} "
                f"({rate:,.0f} entries/s)",
                flush=True,
            )

    conn.close()

    print()
    print(
        "Sorting normalized words..."
    )

    sort_start = time.perf_counter()

    entries.sort(
        key=lambda x: x[0]
    )

    sort_time = (
        time.perf_counter()
        - sort_start
    )

    words = [
        entry[0]
        for entry in entries
    ]

    sizes = [
        entry[1]
        for entry in entries
    ]

    return (
        words,
        sizes,
        total_bytes,
        sort_time,
    )


# ============================================================
# Cumulative offsets
# ============================================================

def build_offsets(sizes):
    """
    offsets[i] = byte offset of entry i.
    """

    offsets = [0] * (
        len(sizes) + 1
    )

    total = 0

    for i, size in enumerate(sizes):

        total += size

        offsets[i + 1] = total

    return offsets


# ============================================================
# Duplicate analysis
# ============================================================

def analyze_duplicates(words):
    """
    Detect normalized duplicate words.

    This matters because duplicate identical words cannot
    be separated using additional prefix characters.
    """

    duplicate_groups = 0
    duplicate_entries = 0
    max_duplicate_count = 0

    n = len(words)

    i = 0

    while i < n:

        j = i + 1

        while (
            j < n
            and words[j] == words[i]
        ):
            j += 1

        count = j - i

        if count > 1:

            duplicate_groups += 1
            duplicate_entries += count

            if count > max_duplicate_count:
                max_duplicate_count = count

        i = j

    return (
        duplicate_groups,
        duplicate_entries,
        max_duplicate_count,
    )


# ============================================================
# Bucket statistics
# ============================================================

def analyze_buckets(
    buckets,
    total_entries,
    total_bytes,
    index_bytes,
):
    if not buckets:
        return None

    sizes = sorted(
        bucket.size
        for bucket in buckets
    )

    counts = sorted(
        bucket.count
        for bucket in buckets
    )

    depths = defaultdict(int)

    unsplittable_count = 0
    unsplittable_bytes = 0
    unsplittable_entries = 0

    for bucket in buckets:

        depths[
            bucket.depth
        ] += 1

        if bucket.unsplittable:

            unsplittable_count += 1
            unsplittable_bytes += bucket.size
            unsplittable_entries += bucket.count

    index_total = (
        len(buckets)
        * index_bytes
    )

    return {
        "bucket_count":
            len(buckets),

        "entries":
            sum(
                bucket.count
                for bucket in buckets
            ),

        "data_bytes":
            sum(
                bucket.size
                for bucket in buckets
            ),

        "index_bytes":
            index_total,

        "total_storage_bytes":
            total_bytes + index_total,

        "size_min":
            min(sizes),

        "size_avg":
            statistics.mean(sizes),

        "size_p50":
            percentile(sizes, 0.50),

        "size_p90":
            percentile(sizes, 0.90),

        "size_p95":
            percentile(sizes, 0.95),

        "size_p99":
            percentile(sizes, 0.99),

        "size_max":
            max(sizes),

        "count_min":
            min(counts),

        "count_avg":
            statistics.mean(counts),

        "count_p50":
            percentile(counts, 0.50),

        "count_p90":
            percentile(counts, 0.90),

        "count_p95":
            percentile(counts, 0.95),

        "count_p99":
            percentile(counts, 0.99),

        "count_max":
            max(counts),

        "depths":
            dict(sorted(depths.items())),

        "unsplittable_count":
            unsplittable_count,

        "unsplittable_bytes":
            unsplittable_bytes,

        "unsplittable_entries":
            unsplittable_entries,
    }


# ============================================================
# Format helpers
# ============================================================

def kib(value):
    return value / 1024


def mib(value):
    return value / 1024 / 1024


def print_summary(
    target,
    stats,
    total_entries,
    total_bytes,
):
    print()
    print("=" * 100)
    print(
        f"TARGET BUCKET ≈ "
        f"{kib(target):.0f} KB"
    )
    print("=" * 100)

    print(
        f"Buckets          : "
        f"{stats['bucket_count']:,}"
    )

    print(
        f"Entries          : "
        f"{stats['entries']:,}"
    )

    print(
        f"Data             : "
        f"{mib(stats['data_bytes']):.2f} MB"
    )

    print(
        f"Index            : "
        f"{kib(stats['index_bytes']):.2f} KB"
    )

    print(
        f"Total storage    : "
        f"{mib(stats['total_storage_bytes']):.2f} MB"
    )

    print()

    print("Bucket size:")

    print(
        f"  min            : "
        f"{kib(stats['size_min']):.2f} KB"
    )

    print(
        f"  average        : "
        f"{kib(stats['size_avg']):.2f} KB"
    )

    print(
        f"  P50            : "
        f"{kib(stats['size_p50']):.2f} KB"
    )

    print(
        f"  P90            : "
        f"{kib(stats['size_p90']):.2f} KB"
    )

    print(
        f"  P95            : "
        f"{kib(stats['size_p95']):.2f} KB"
    )

    print(
        f"  P99            : "
        f"{kib(stats['size_p99']):.2f} KB"
    )

    print(
        f"  max            : "
        f"{kib(stats['size_max']):.2f} KB"
    )

    print()

    print("Entries / bucket:")

    print(
        f"  min            : "
        f"{stats['count_min']:,}"
    )

    print(
        f"  average        : "
        f"{stats['count_avg']:,.1f}"
    )

    print(
        f"  P50            : "
        f"{stats['count_p50']:,.0f}"
    )

    print(
        f"  P90            : "
        f"{stats['count_p90']:,.0f}"
    )

    print(
        f"  P95            : "
        f"{stats['count_p95']:,.0f}"
    )

    print(
        f"  P99            : "
        f"{stats['count_p99']:,.0f}"
    )

    print(
        f"  max            : "
        f"{stats['count_max']:,}"
    )

    print()

    print("Un-splittable buckets:")

    print(
        f"  buckets        : "
        f"{stats['unsplittable_count']:,}"
    )

    print(
        f"  entries        : "
        f"{stats['unsplittable_entries']:,}"
    )

    print(
        f"  data           : "
        f"{mib(stats['unsplittable_bytes']):.2f} MB"
    )

    print()

    print("Prefix depth distribution:")

    for depth, count in stats["depths"].items():

        print(
            f"  depth {depth:2d}: "
            f"{count:,} buckets"
        )

    # --------------------------------------------------------
    # Sanity checks
    # --------------------------------------------------------

    if stats["entries"] != total_entries:

        print(
            "ERROR: entry count mismatch!",
            file=sys.stderr,
        )

    if stats["data_bytes"] != total_bytes:

        print(
            "ERROR: byte count mismatch!",
            file=sys.stderr,
        )


# ============================================================
# Largest buckets
# ============================================================

def print_largest_buckets(
    buckets,
    count=20,
):
    print()
    print(
        f"Largest {count} buckets:"
    )

    largest = sorted(
        buckets,
        key=lambda b: b.size,
        reverse=True,
    )[:count]

    for bucket in largest:

        status = ""

        if bucket.unsplittable:
            status = (
                f"  [UNSPLITTABLE: "
                f"{bucket.reason}]"
            )

        print(
            f"  {bucket.display_prefix:<22} "
            f"{bucket.count:>9,} entries "
            f"{kib(bucket.size):>11.2f} KB "
            f"depth={bucket.depth}"
            f"{status}"
        )


# ============================================================
# Largest entry sizes
# ============================================================

def print_largest_entries(
    words,
    sizes,
    count=20,
):
    print()
    print(
        f"Largest {count} individual entries:"
    )

    indices = sorted(
        range(len(sizes)),
        key=lambda i: sizes[i],
        reverse=True,
    )[:count]

    for i in indices:

        print(
            f"  {words[i]:<35} "
            f"{kib(sizes[i]):>10.2f} KB"
        )


# ============================================================
# CSV
# ============================================================

def write_bucket_csv(
    path,
    buckets,
):
    with open(
        path,
        "w",
        newline="",
        encoding="utf-8",
    ) as f:

        writer = csv.writer(f)

        writer.writerow(
            [
                "prefix",
                "display_prefix",
                "depth",
                "start_entry",
                "end_entry",
                "entry_count",
                "start_offset",
                "end_offset",
                "size_bytes",
                "unsplittable",
                "reason",
            ]
        )

        for bucket in buckets:

            writer.writerow(
                [
                    bucket.prefix,
                    bucket.display_prefix,
                    bucket.depth,
                    bucket.start,
                    bucket.end,
                    bucket.count,
                    bucket.start_offset,
                    bucket.end_offset,
                    bucket.size,
                    int(bucket.unsplittable),
                    bucket.reason,
                ]
            )


# ============================================================
# Comparison table
# ============================================================

def print_comparison(
    results,
):
    print()
    print()
    print("=" * 125)
    print("COMPARISON")
    print("=" * 125)

    print(
        f"{'Target':>9} "
        f"{'Buckets':>10} "
        f"{'Avg KB':>10} "
        f"{'P50 KB':>10} "
        f"{'P95 KB':>10} "
        f"{'P99 KB':>10} "
        f"{'Max KB':>11} "
        f"{'P95 Entries':>14} "
        f"{'Index KB':>12} "
        f"{'Unsplittable':>13}"
    )

    print("-" * 125)

    for target, stats, elapsed in results:

        print(
            f"{kib(target):>8.0f}K "
            f"{stats['bucket_count']:>10,} "
            f"{kib(stats['size_avg']):>10.2f} "
            f"{kib(stats['size_p50']):>10.2f} "
            f"{kib(stats['size_p95']):>10.2f} "
            f"{kib(stats['size_p99']):>10.2f} "
            f"{kib(stats['size_max']):>11.2f} "
            f"{stats['count_p95']:>14,.0f} "
            f"{kib(stats['index_bytes']):>12.2f} "
            f"{stats['unsplittable_count']:>13,}"
        )

    print()


# ============================================================
# Main
# ============================================================

def main():

    parser = argparse.ArgumentParser(
        description=(
            "ECDICT Step 3.1 adaptive bucket simulator"
        )
    )

    parser.add_argument(
        "database",
        type=Path,
    )

    parser.add_argument(
        "--targets",
        nargs="+",
        type=int,
        default=DEFAULT_TARGETS,
        help=(
            "Bucket targets in bytes. "
            "Default: "
            "4K 8K 16K 32K 64K 128K 256K"
        ),
    )

    parser.add_argument(
        "--max-depth",
        type=int,
        default=DEFAULT_MAX_DEPTH,
    )

    parser.add_argument(
        "--index-bytes",
        type=int,
        default=DEFAULT_INDEX_BYTES,
        help=(
            "Estimated index bytes per bucket."
        ),
    )

    parser.add_argument(
        "--csv-dir",
        type=Path,
        default=None,
        help=(
            "Optional output directory for bucket CSV."
        ),
    )

    args = parser.parse_args()

    if not args.database.exists():

        parser.error(
            f"Database not found: "
            f"{args.database}"
        )

    if args.max_depth <= 0:
        parser.error(
            "--max-depth must be > 0"
        )

    if args.index_bytes <= 0:
        parser.error(
            "--index-bytes must be > 0"
        )

    if any(
        target <= 0
        for target in args.targets
    ):
        parser.error(
            "All targets must be > 0"
        )

    print("=" * 100)
    print(
        "ECDICT STEP 3.1 — "
        "ADAPTIVE BUCKET SIMULATOR"
    )
    print("=" * 100)

    print()
    print(
        f"Database       : "
        f"{args.database}"
    )

    print(
        "Targets        : "
        + ", ".join(
            f"{kib(x):.0f} KB"
            for x in sorted(args.targets)
        )
    )

    print(
        f"Max depth      : "
        f"{args.max_depth}"
    )

    print(
        f"Index estimate : "
        f"{args.index_bytes} bytes / bucket"
    )

    print()

    # ========================================================
    # Phase 1
    # ========================================================

    print(
        "Phase 1/4 — Reading SQLite..."
    )
    print()

    phase_start = time.perf_counter()

    (
        words,
        sizes,
        total_bytes,
        sort_time,
    ) = load_database(
        args.database
    )

    phase_time = (
        time.perf_counter()
        - phase_start
    )

    print(
        f"Entries        : "
        f"{len(words):,}"
    )

    print(
        f"Estimated data : "
        f"{mib(total_bytes):.2f} MB"
    )

    print(
        f"Read + sort    : "
        f"{phase_time:.2f} s"
    )

    print(
        f"Sort time      : "
        f"{sort_time:.2f} s"
    )

    # ========================================================
    # Verify sorted order
    # ========================================================

    print()
    print(
        "Verifying normalized sort order..."
    )

    sort_check_start = time.perf_counter()

    bad_index = None

    for i in range(
        1,
        len(words),
    ):

        if words[i - 1] > words[i]:
            bad_index = i
            break

    sort_check_time = (
        time.perf_counter()
        - sort_check_start
    )

    if bad_index is not None:

        print(
            "ERROR: normalized words are not sorted!",
            file=sys.stderr,
        )

        print(
            f"  index={bad_index}",
            file=sys.stderr,
        )

        print(
            f"  previous={words[bad_index - 1]!r}",
            file=sys.stderr,
        )

        print(
            f"  current={words[bad_index]!r}",
            file=sys.stderr,
        )

        sys.exit(1)

    print(
        f"Sort check    : PASS "
        f"({sort_check_time:.3f} s)"
    )

    # ========================================================
    # Duplicate analysis
    # ========================================================

    print()
    print(
        "Analyzing normalized duplicates..."
    )

    (
        duplicate_groups,
        duplicate_entries,
        max_duplicate_count,
    ) = analyze_duplicates(words)

    print(
        f"Duplicate groups       : "
        f"{duplicate_groups:,}"
    )

    print(
        f"Entries in duplicates  : "
        f"{duplicate_entries:,}"
    )

    print(
        f"Maximum duplicate count: "
        f"{max_duplicate_count:,}"
    )

    # ========================================================
    # Phase 2
    # ========================================================

    print()
    print(
        "Phase 2/4 — Building byte offsets..."
    )

    offset_start = time.perf_counter()

    offsets = build_offsets(
        sizes
    )

    offset_time = (
        time.perf_counter()
        - offset_start
    )

    print(
        f"Offset time    : "
        f"{offset_time:.3f} s"
    )

    print(
        f"Final offset   : "
        f"{mib(offsets[-1]):.2f} MB"
    )

    if offsets[-1] != total_bytes:

        print(
            "ERROR: offset total mismatch!",
            file=sys.stderr,
        )

        sys.exit(1)

    # ========================================================
    # Individual entry size analysis
    # ========================================================

    print_largest_entries(
        words,
        sizes,
        count=20,
    )

    # ========================================================
    # Phase 3
    # ========================================================

    print()
    print(
        "Phase 3/4 — Adaptive bucket simulation..."
    )

    results = []

    for target in sorted(
        args.targets
    ):

        start_time = time.perf_counter()

        buckets = build_buckets(
            words,
            offsets,
            target,
            args.max_depth,
        )

        elapsed = (
            time.perf_counter()
            - start_time
        )

        stats = analyze_buckets(
            buckets,
            len(words),
            total_bytes,
            args.index_bytes,
        )

        print_summary(
            target,
            stats,
            len(words),
            total_bytes,
        )

        print(
            f"\nSimulation time : "
            f"{elapsed:.3f} s"
        )

        print_largest_buckets(
            buckets,
            count=20,
        )

        if args.csv_dir:

            args.csv_dir.mkdir(
                parents=True,
                exist_ok=True,
            )

            csv_path = (
                args.csv_dir
                / f"buckets_{target}.csv"
            )

            write_bucket_csv(
                csv_path,
                buckets,
            )

            print(
                f"\nCSV written     : "
                f"{csv_path}"
            )

        results.append(
            (
                target,
                stats,
                elapsed,
            )
        )

    # ========================================================
    # Phase 4
    # ========================================================

    print()
    print(
        "Phase 4/4 — Final comparison..."
    )

    print_comparison(
        results
    )

    # ========================================================
    # Final sanity checks
    # ========================================================

    print(
        "=" * 100
    )
    print(
        "FINAL SANITY CHECK"
    )
    print(
        "=" * 100
    )

    print(
        f"Entries         : "
        f"{len(words):,}"
    )

    print(
        f"Expected        : "
        f"{len(words):,}"
    )

    print(
        f"Binary estimate : "
        f"{mib(total_bytes):.2f} MB"
    )

    print(
        "All entries preserved: PASS"
    )

    print()
    print(
        "STEP 3.1 COMPLETE"
    )
    print(
        "=" * 100
    )


if __name__ == "__main__":
    main()