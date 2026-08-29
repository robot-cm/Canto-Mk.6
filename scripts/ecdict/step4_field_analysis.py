#!/usr/bin/env python3

import argparse
import sqlite3
import time
import unicodedata
from pathlib import Path


FIELDS = [
    "word",
    "phonetic",
    "translation",
    "definition",
    "pos",
    "exchange",
]


def normalize_word(value):
    if value is None:
        return ""
    return unicodedata.normalize("NFKC", value).strip().lower()


def utf8_len(value):
    if not value:
        return 0
    return len(value.encode("utf-8"))


def percentile(sorted_values, p):
    if not sorted_values:
        return 0

    if len(sorted_values) == 1:
        return sorted_values[0]

    index = (len(sorted_values) - 1) * p
    lo = int(index)
    hi = min(lo + 1, len(sorted_values) - 1)

    if lo == hi:
        return sorted_values[lo]

    fraction = index - lo

    return (
        sorted_values[lo] * (1 - fraction)
        + sorted_values[hi] * fraction
    )


def format_bytes(value):
    if value < 1024:
        return f"{value:.0f} B"

    if value < 1024 * 1024:
        return f"{value / 1024:.2f} KB"

    if value < 1024 * 1024 * 1024:
        return f"{value / 1024 / 1024:.2f} MB"

    return f"{value / 1024 / 1024 / 1024:.2f} GB"


def format_int(value):
    return f"{int(value):,}"


def analyze_field(values):
    values.sort()

    total = sum(values)
    count = len(values)

    nonzero = sum(1 for x in values if x > 0)

    return {
        "total": total,
        "average": total / count if count else 0,
        "p50": percentile(values, 0.50),
        "p90": percentile(values, 0.90),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "max": values[-1] if values else 0,
        "nonzero": nonzero,
        "empty": count - nonzero,
    }


def main():

    parser = argparse.ArgumentParser(
        description="ECDICT Step 4.0 field size analysis"
    )

    parser.add_argument(
        "database",
        type=Path,
    )

    args = parser.parse_args()

    db = args.database.resolve()

    print("=" * 100)
    print("ECDICT STEP 4.0 — REAL FIELD SIZE ANALYSIS")
    print("=" * 100)
    print()
    print(f"Database : {db}")
    print()
    print("Fields:")
    print("  word")
    print("  phonetic")
    print("  translation")
    print("  definition")
    print("  pos")
    print("  exchange")
    print()
    print("tag     : NOT INCLUDED")
    print()

    conn = sqlite3.connect(
        f"file:{db}?mode=ro",
        uri=True,
    )

    conn.execute("PRAGMA query_only=ON")

    query = """
        SELECT
            word,
            phonetic,
            translation,
            definition,
            pos,
            exchange
        FROM stardict
        ORDER BY word
    """

    print("Reading SQLite...")
    print()

    field_values = {
        field: []
        for field in FIELDS
    }

    field_totals = {
        field: 0
        for field in FIELDS
    }

    total_entries = 0

    total_payload = 0

    largest_entries = []

    start = time.perf_counter()

    cursor = conn.execute(query)

    for row in cursor:

        total_entries += 1

        entry_size = 0

        for i, field in enumerate(FIELDS):

            value = row[i]

            size = utf8_len(value)

            field_values[field].append(size)

            field_totals[field] += size

            entry_size += size

        total_payload += entry_size

        # 保留最大的 50 条 entry
        word = normalize_word(row[0])

        largest_entries.append(
            (entry_size, word, row)
        )

        if len(largest_entries) > 50:

            largest_entries.sort(
                key=lambda x: x[0],
                reverse=True,
            )

            del largest_entries[50:]

        if total_entries % 500_000 == 0:

            elapsed = time.perf_counter() - start

            rate = total_entries / elapsed

            print(
                f"  processed "
                f"{total_entries:,} "
                f"({rate:,.0f} entries/s)"
            )

    conn.close()

    elapsed = time.perf_counter() - start

    print()
    print(
        f"Entries       : "
        f"{format_int(total_entries)}"
    )

    print(
        f"Raw field data: "
        f"{format_bytes(total_payload)}"
    )

    print(
        f"Read time     : "
        f"{elapsed:.2f} s"
    )

    print()

    # ------------------------------------------------------------------
    # Field statistics
    # ------------------------------------------------------------------

    print("=" * 100)
    print("FIELD SIZE STATISTICS")
    print("=" * 100)

    print()

    header = (
        f"{'Field':<14}"
        f"{'Total':>14}"
        f"{'Avg':>12}"
        f"{'P50':>10}"
        f"{'P90':>10}"
        f"{'P95':>10}"
        f"{'P99':>10}"
        f"{'Max':>12}"
        f"{'Empty':>12}"
        f"{'Share':>9}"
    )

    print(header)
    print("-" * len(header))

    statistics = {}

    for field in FIELDS:

        stats = analyze_field(
            field_values[field]
        )

        statistics[field] = stats

        share = (
            stats["total"] / total_payload * 100
            if total_payload
            else 0
        )

        print(
            f"{field:<14}"
            f"{format_bytes(stats['total']):>14}"
            f"{format_bytes(stats['average']):>12}"
            f"{format_bytes(stats['p50']):>10}"
            f"{format_bytes(stats['p90']):>10}"
            f"{format_bytes(stats['p95']):>10}"
            f"{format_bytes(stats['p99']):>10}"
            f"{format_bytes(stats['max']):>12}"
            f"{format_int(stats['empty']):>12}"
            f"{share:>8.2f}%"
        )

    print()

    # ------------------------------------------------------------------
    # Entry payload distribution
    # ------------------------------------------------------------------

    entry_sizes = []

    for i in range(total_entries):

        size = 0

        for field in FIELDS:
            size += field_values[field][i]

        entry_sizes.append(size)

    entry_sizes.sort()

    print("=" * 100)
    print("COMBINED ENTRY PAYLOAD")
    print("=" * 100)
    print()

    print(
        f"Total       : "
        f"{format_bytes(sum(entry_sizes))}"
    )

    print(
        f"Average     : "
        f"{format_bytes(sum(entry_sizes) / len(entry_sizes))}"
    )

    print(
        f"P50         : "
        f"{format_bytes(percentile(entry_sizes, 0.50))}"
    )

    print(
        f"P90         : "
        f"{format_bytes(percentile(entry_sizes, 0.90))}"
    )

    print(
        f"P95         : "
        f"{format_bytes(percentile(entry_sizes, 0.95))}"
    )

    print(
        f"P99         : "
        f"{format_bytes(percentile(entry_sizes, 0.99))}"
    )

    print(
        f"Max         : "
        f"{format_bytes(entry_sizes[-1])}"
    )

    print()

    # ------------------------------------------------------------------
    # Largest entries
    # ------------------------------------------------------------------

    largest_entries.sort(
        key=lambda x: x[0],
        reverse=True,
    )

    print("=" * 100)
    print("LARGEST 30 ENTRIES")
    print("=" * 100)
    print()

    for size, word, row in largest_entries[:30]:

        print(
            f"{word[:30]:<30}"
            f"{format_bytes(size):>12}"
        )

        for i, field in enumerate(FIELDS):

            value = row[i]

            if value:

                print(
                    f"    {field:<14}"
                    f"{format_bytes(utf8_len(value)):>12}"
                )

        print()

    # ------------------------------------------------------------------
    # Storage simulation
    # ------------------------------------------------------------------

    print("=" * 100)
    print("BINARY STORAGE SIMULATION")
    print("=" * 100)
    print()

    print(
        "This estimates several possible entry layouts."
    )

    print()

    # Layout A:
    #
    # word_len       uint16
    # phonetic_len   uint16
    # translation_len uint32
    # definition_len uint32
    # pos_len        uint8
    # exchange_len   uint16
    #
    # Header = 17 bytes
    #
    # No alignment.

    layout_a_header = 17

    # Layout B:
    #
    # 6 × uint32 lengths
    #
    layout_b_header = 24

    # Layout C:
    #
    # Fixed entry header:
    #
    # word offset       uint32
    # phonetic offset   uint32
    # translation offset uint32
    # definition offset uint32
    # pos offset        uint32
    # exchange offset   uint32
    #
    # 24 bytes
    #
    layout_c_header = 24

    layouts = [
        (
            "A: inline + compact lengths",
            layout_a_header,
        ),
        (
            "B: inline + 6×uint32 lengths",
            layout_b_header,
        ),
        (
            "C: offset based header",
            layout_c_header,
        ),
    ]

    for name, header_size in layouts:

        estimated = (
            total_payload
            + total_entries * header_size
        )

        print(
            f"{name:<38}"
            f"{format_bytes(estimated):>14}"
        )

    print()

    # ------------------------------------------------------------------
    # Important observations
    # ------------------------------------------------------------------

    print("=" * 100)
    print("PRELIMINARY OBSERVATIONS")
    print("=" * 100)
    print()

    definition_share = (
        field_totals["definition"]
        / total_payload * 100
        if total_payload
        else 0
    )

    translation_share = (
        field_totals["translation"]
        / total_payload * 100
        if total_payload
        else 0
    )

    exchange_share = (
        field_totals["exchange"]
        / total_payload * 100
        if total_payload
        else 0
    )

    print(
        f"Definition share : "
        f"{definition_share:.2f}%"
    )

    print(
        f"Translation share: "
        f"{translation_share:.2f}%"
    )

    print(
        f"Exchange share   : "
        f"{exchange_share:.2f}%"
    )

    print()

    print(
        "NOTE:"
    )

    print(
        "The binary format has NOT been finalized."
    )

    print(
        "This step only measures the real data."
    )

    print()

    print("=" * 100)
    print("STEP 4.0 COMPLETE")
    print("=" * 100)


if __name__ == "__main__":
    main()