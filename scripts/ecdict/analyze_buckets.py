#!/usr/bin/env python3

import argparse
import sqlite3
import string
from collections import Counter
from pathlib import Path


ALPHABET = string.ascii_lowercase


def normalize_word(word: str) -> str:
    """
    用于索引的规范化 key。

    当前版本：
    - Unicode lower
    - 去掉首尾空白
    - 非 ASCII 字符暂时保留
    - 不修改原始 word

    后续正式 exporter 再确定最终 normalization 规则。
    """
    return word.strip().lower()


def bucket_key(word: str, depth: int) -> str:
    """
    根据前 depth 个字符生成 bucket。

    为了避免 '/'、引号等特殊字符直接进入文件路径：
    - a-z 使用自身
    - 其它字符统一映射到 '_'
    """
    key = normalize_word(word)

    result = []

    for ch in key[:depth]:
        if ch in ALPHABET:
            result.append(ch)
        else:
            result.append("_")

    while len(result) < depth:
        result.append("_")

    return "".join(result)


def analyze_depth(conn, depth):
    print()
    print("=" * 80)
    print(f"DEPTH = {depth}")
    print("=" * 80)

    counter = Counter()

    cursor = conn.execute(
        "SELECT word FROM stardict"
    )

    total = 0

    for (word,) in cursor:
        key = bucket_key(word, depth)
        counter[key] += 1
        total += 1

        if total % 500_000 == 0:
            print(f"  processed {total:,}")

    bucket_count = len(counter)

    counts = list(counter.values())

    counts.sort(reverse=True)

    print()
    print(f"Total entries : {total:,}")
    print(f"Buckets       : {bucket_count:,}")
    print(f"Average       : {total / bucket_count:.2f}")
    print(f"Maximum       : {counts[0]:,}")
    print(f"Minimum       : {counts[-1]:,}")

    print()
    print("Percentiles:")

    for p in [50, 90, 95, 99, 99.9]:
        index = min(
            int(len(counts) * (1 - p / 100)),
            len(counts) - 1,
        )

        print(
            f"  P{p:<5}: {counts[index]:,}"
        )

    print()
    print("Buckets by size:")

    ranges = [
        (0, 10),
        (10, 50),
        (50, 100),
        (100, 500),
        (500, 1_000),
        (1_000, 5_000),
        (5_000, 10_000),
        (10_000, 50_000),
        (50_000, 100_000),
        (100_000, 1_000_000_000),
    ]

    for low, high in ranges:
        count = sum(
            low <= n < high
            for n in counts
        )

        if count:
            print(
                f"  {low:>8,} - {high:>12,}: "
                f"{count:,} buckets"
            )

    print()
    print("Top 30 largest buckets:")

    for key, count in counter.most_common(30):
        print(
            f"  {key:<8} {count:>10,}"
        )

    return counter


def main():
    parser = argparse.ArgumentParser(
        description="Analyze ECDICT bucket distributions."
    )

    parser.add_argument(
        "database",
        type=Path,
        help="Path to stardict.db",
    )

    args = parser.parse_args()

    if not args.database.exists():
        raise SystemExit(
            f"Database not found: {args.database}"
        )

    print(f"Database: {args.database}")

    conn = sqlite3.connect(
        f"file:{args.database.resolve()}?mode=ro",
        uri=True,
    )

    conn.execute("PRAGMA query_only = ON")

    print()
    print("Testing database...")

    row = conn.execute(
        "SELECT COUNT(*) FROM stardict"
    ).fetchone()

    print(
        f"Entries: {row[0]:,}"
    )

    # 我们重点比较 1~5 级前缀。
    for depth in range(1, 6):
        analyze_depth(
            conn,
            depth,
        )

    conn.close()

    print()
    print("=" * 80)
    print("DONE")
    print("=" * 80)


if __name__ == "__main__":
    main()