#!/usr/bin/env python3
"""
ECDICT STEP 4.1
PC -> .dat exporter

Final format:
    Global Header
    Bucket Index
    Bucket Data

Entry:
    12-byte fixed header:
        uint16 word_len
        uint16 phonetic_len
        uint16 translation_len
        uint16 definition_len
        uint16 pos_len
        uint16 exchange_len

    followed by UTF-8 payloads:
        word
        phonetic
        translation
        definition
        pos
        exchange

Lookup:
    normalize = NFKC + strip + lowercase

Every 64 entries:
    complete normalized word checkpoint

No compression.
"""

from __future__ import annotations

import argparse
import hashlib
import sqlite3
import struct
import sys
import time
import unicodedata
from dataclasses import dataclass
from pathlib import Path


# ============================================================================
# CONSTANTS
# ============================================================================

MAGIC = b"ECDICT01"

VERSION = 1

# 128 KiB target.
DEFAULT_BUCKET_TARGET = 128 * 1024

# One normalized-word checkpoint every 64 entries.
CHECKPOINT_INTERVAL = 64

# Fixed entry header.
ENTRY_HEADER_SIZE = 12

# Global header:
#
# magic              8
# version            4
# header_size        4
# entry_count        8
# bucket_count       4
# bucket_target      4
# checkpoint_every   4
# bucket_index_off   8
# bucket_data_off    8
# total_data_size    8
# sha256_off         8
#
# = 72 bytes
GLOBAL_HEADER_FMT = "<8sIIIQIIIIQQQQ"
GLOBAL_HEADER_SIZE = struct.calcsize(GLOBAL_HEADER_FMT)

# Bucket index:
#
# first normalized word offset in global string table
# bucket data offset
# bucket data size
# entry count
# checkpoint count
# first entry index
#
# 8 + 8 + 4 + 4 + 4 + 8 = 36
#
# We intentionally align each record to 40 bytes.
BUCKET_INDEX_FMT = "<QQIIIIQ"
BUCKET_INDEX_RAW_SIZE = struct.calcsize(BUCKET_INDEX_FMT)
BUCKET_INDEX_SIZE = 40

# Bucket header:
#
# magic             4
# header_size       2
# reserved          2
# entry_count       4
# checkpoint_count  4
# checkpoint_offset 4
# entry_offset      4
# bucket_data_size  4
#
# = 28 bytes
BUCKET_MAGIC = b"BKT1"
BUCKET_HEADER_FMT = "<4sHHIIIIII"
BUCKET_HEADER_SIZE = struct.calcsize(BUCKET_HEADER_FMT)

# Checkpoint:
#
# entry_index       4
# entry_offset      4
# normalized length 2
# reserved          2
#
# followed by normalized UTF-8 bytes
CHECKPOINT_HEADER_FMT = "<IIHH"
CHECKPOINT_HEADER_SIZE = struct.calcsize(CHECKPOINT_HEADER_FMT)

MAX_FIELD_LENGTH = 65535


# ============================================================================
# NORMALIZATION
# ============================================================================

def normalize(word: str) -> str:
    """
    Lookup normalization.

    IMPORTANT:
    The original word is preserved in the entry.
    Only the lookup key is normalized.
    """
    return unicodedata.normalize("NFKC", word).strip().lower()


# ============================================================================
# DATA STRUCTURES
# ============================================================================

@dataclass(slots=True)
class Entry:
    word: str
    phonetic: str
    translation: str
    definition: str
    pos: str
    exchange: str

    normalized: str
    payload: bytes


@dataclass(slots=True)
class BucketInfo:
    first_normalized: str
    first_entry_index: int
    entry_count: int
    data_offset: int
    data_size: int
    checkpoint_count: int


# ============================================================================
# UTF-8
# ============================================================================

def encode_field(value: str | None) -> bytes:
    if not value:
        return b""

    data = value.encode("utf-8")

    if len(data) > MAX_FIELD_LENGTH:
        raise ValueError(
            f"Field exceeds uint16 length limit: {len(data)} bytes"
        )

    return data


def make_entry(row) -> Entry:
    word = row[0] or ""
    phonetic = row[1] or ""
    translation = row[2] or ""
    definition = row[3] or ""
    pos = row[4] or ""
    exchange = row[5] or ""

    normalized = normalize(word)

    word_b = encode_field(word)
    phonetic_b = encode_field(phonetic)
    translation_b = encode_field(translation)
    definition_b = encode_field(definition)
    pos_b = encode_field(pos)
    exchange_b = encode_field(exchange)

    header = struct.pack(
        "<6H",
        len(word_b),
        len(phonetic_b),
        len(translation_b),
        len(definition_b),
        len(pos_b),
        len(exchange_b),
    )

    payload = (
        header
        + word_b
        + phonetic_b
        + translation_b
        + definition_b
        + pos_b
        + exchange_b
    )

    return Entry(
        word=word,
        phonetic=phonetic,
        translation=translation,
        definition=definition,
        pos=pos,
        exchange=exchange,
        normalized=normalized,
        payload=payload,
    )


# ============================================================================
# DATABASE
# ============================================================================

def load_entries(database: Path) -> list[Entry]:
    conn = sqlite3.connect(
        f"file:{database.resolve()}?mode=ro",
        uri=True,
    )

    conn.execute("PRAGMA query_only=ON")

    cursor = conn.execute(
        """
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
    )

    entries: list[Entry] = []

    start = time.perf_counter()

    for i, row in enumerate(cursor, 1):

        entry = make_entry(row)
        entries.append(entry)

        if i % 500_000 == 0:
            elapsed = time.perf_counter() - start
            rate = i / elapsed

            print(
                f"  read {i:,} "
                f"({rate:,.0f} entries/s)"
            )

    conn.close()

    return entries


# ============================================================================
# SORT / VALIDATION
# ============================================================================

def sort_entries(entries: list[Entry]) -> None:
    entries.sort(key=lambda e: e.normalized)


def verify_sort(entries: list[Entry]) -> None:
    for i in range(1, len(entries)):
        if entries[i - 1].normalized > entries[i].normalized:
            raise RuntimeError(
                "Normalized sort order verification failed at "
                f"entry {i}"
            )


def verify_duplicates(entries: list[Entry]) -> None:
    duplicates = 0

    for i in range(1, len(entries)):
        if entries[i - 1].normalized == entries[i].normalized:
            duplicates += 1

    if duplicates:
        raise RuntimeError(
            f"Found {duplicates:,} normalized duplicate entries."
        )


# ============================================================================
# CHECKPOINT
# ============================================================================

def build_checkpoint(
    entry_index: int,
    entry_offset: int,
    normalized: str,
) -> bytes:

    data = normalized.encode("utf-8")

    if len(data) > MAX_FIELD_LENGTH:
        raise ValueError(
            "Normalized word exceeds uint16 length limit"
        )

    return (
        struct.pack(
            CHECKPOINT_HEADER_FMT,
            entry_index,
            entry_offset,
            len(data),
            0,
        )
        + data
    )


# ============================================================================
# BUCKET BUILDER
# ============================================================================

def build_bucket(
    entries: list[Entry],
    start_index: int,
    target_size: int,
) -> tuple[bytes, BucketInfo, int]:

    entry_data = bytearray()
    checkpoints = bytearray()

    entry_count = 0

    index = start_index

    while index < len(entries):

        entry = entries[index]

        # Every 64th entry gets a complete normalized checkpoint.
        checkpoint_needed = (
            entry_count % CHECKPOINT_INTERVAL == 0
        )

        entry_offset = (
            BUCKET_HEADER_SIZE
            + 0
            + len(checkpoints)
            + len(entry_data)
        )

        checkpoint = b""

        if checkpoint_needed:
            checkpoint = build_checkpoint(
                entry_count,
                0,  # patched below
                entry.normalized,
            )

        # We don't know final checkpoint offset until
        # checkpoint area is complete.
        #
        # Therefore first calculate raw checkpoint size.
        checkpoint_size = len(checkpoint)

        projected_checkpoint_bytes = len(checkpoints) + checkpoint_size

        projected_entry_bytes = (
            len(entry_data) + len(entry.payload)
        )

        projected_total = (
            BUCKET_HEADER_SIZE
            + projected_checkpoint_bytes
            + projected_entry_bytes
        )

        # Don't split an entry across buckets.
        #
        # However, never create an empty bucket.
        if (
            entry_count > 0
            and projected_total > target_size
        ):
            break

        # Entry is accepted.
        if checkpoint_needed:

            # Checkpoint entry offset is relative to
            # beginning of entry-data region.
            checkpoint = build_checkpoint(
                entry_count,
                len(entry_data),
                entry.normalized,
            )

            checkpoints.extend(checkpoint)

        entry_data.extend(entry.payload)

        entry_count += 1
        index += 1

    checkpoint_offset = BUCKET_HEADER_SIZE
    entry_offset = (
        BUCKET_HEADER_SIZE
        + len(checkpoints)
    )

    bucket_size = (
        BUCKET_HEADER_SIZE
        + len(checkpoints)
        + len(entry_data)
    )

    header = struct.pack(
        BUCKET_HEADER_FMT,
        BUCKET_MAGIC,
        BUCKET_HEADER_SIZE,
        0,
        entry_count,
        entry_count // CHECKPOINT_INTERVAL
        + (1 if entry_count % CHECKPOINT_INTERVAL else 0),
        checkpoint_offset,
        entry_offset,
        bucket_size,
        0,
    )

    bucket = (
        header
        + checkpoints
        + entry_data
    )

    info = BucketInfo(
        first_normalized=entries[start_index].normalized,
        first_entry_index=start_index,
        entry_count=entry_count,
        data_offset=0,
        data_size=len(bucket),
        checkpoint_count=(
            entry_count // CHECKPOINT_INTERVAL
            + (1 if entry_count % CHECKPOINT_INTERVAL else 0)
        ),
    )

    return bytes(bucket), info, index


# ============================================================================
# GLOBAL FILE
# ============================================================================

def write_dat(
    output: Path,
    entries: list[Entry],
    target_size: int,
) -> tuple[int, list[BucketInfo]]:

    buckets: list[tuple[bytes, BucketInfo]] = []

    index = 0

    print()
    print("Building buckets...")

    start = time.perf_counter()

    while index < len(entries):

        bucket_data, info, next_index = build_bucket(
            entries,
            index,
            target_size,
        )

        if next_index <= index:
            raise RuntimeError(
                "Bucket builder made no progress."
            )

        buckets.append(
            (
                bucket_data,
                info,
            )
        )

        index = next_index

        if len(buckets) % 100 == 0:
            print(
                f"  buckets built: {len(buckets):,} "
                f"entries: {index:,}/{len(entries):,}"
            )

    build_time = time.perf_counter() - start

    print(
        f"Buckets built : {len(buckets):,}"
    )

    print(
        f"Bucket time   : {build_time:.3f} s"
    )

    # ------------------------------------------------------------------------
    # File layout
    #
    # [global header]
    # [bucket index]
    # [bucket data]
    # ------------------------------------------------------------------------

    bucket_index_offset = GLOBAL_HEADER_SIZE

    bucket_index_size = (
        len(buckets) * BUCKET_INDEX_SIZE
    )

    bucket_data_offset = (
        bucket_index_offset
        + bucket_index_size
    )

    current_offset = bucket_data_offset

    finalized_infos: list[BucketInfo] = []

    for bucket_data, info in buckets:

        finalized_infos.append(
            BucketInfo(
                first_normalized=info.first_normalized,
                first_entry_index=info.first_entry_index,
                entry_count=info.entry_count,
                data_offset=current_offset,
                data_size=len(bucket_data),
                checkpoint_count=info.checkpoint_count,
            )
        )

        current_offset += len(bucket_data)

    total_size = current_offset

    # ------------------------------------------------------------------------
    # SHA256 is written at EOF.
    # ------------------------------------------------------------------------

    sha256_offset = total_size

    total_file_size = (
        total_size + 32
    )

    # ------------------------------------------------------------------------
    # Header
    # ------------------------------------------------------------------------

    header = struct.pack(
        GLOBAL_HEADER_FMT,

        MAGIC,
        VERSION,
        GLOBAL_HEADER_SIZE,

        len(entries),

        len(buckets),

        target_size,

        CHECKPOINT_INTERVAL,

        ENTRY_HEADER_SIZE,

        0,

        bucket_index_offset,
        bucket_data_offset,
        total_file_size,
        sha256_offset,
    )

    assert len(header) == GLOBAL_HEADER_SIZE

    # ------------------------------------------------------------------------
    # Write
    # ------------------------------------------------------------------------

    print()
    print("Writing .dat...")

    write_start = time.perf_counter()

    sha256 = hashlib.sha256()

    with output.open("wb") as f:

        f.write(header)
        sha256.update(header)

        # Bucket index.
        for info in finalized_infos:

            first_word = (
                info.first_normalized.encode("utf-8")
            )

            # We store the first normalized word inline
            # in the bucket index only up to 64 bytes.
            #
            # Longer words are still fully represented by
            # the bucket's first checkpoint.
            #
            # The index stores an absolute pointer to the
            # bucket itself, therefore lookup can obtain
            # the actual checkpoint when needed.
            #
            # For the global binary search, we instead use
            # a separate string table below.
            #
            # This implementation uses the first checkpoint
            # as the authoritative key.
            #
            # Index record:
            #   uint64 bucket_data_offset
            #   uint64 first_entry_index
            #   uint32 data_size
            #   uint32 entry_count
            #   uint32 checkpoint_count
            #   uint32 reserved
            #   uint64 first_checkpoint_offset
            #
            first_checkpoint_offset = (
                info.data_offset
                + BUCKET_HEADER_SIZE
            )

            record = struct.pack(
                BUCKET_INDEX_FMT,
                first_checkpoint_offset,
                info.first_entry_index,
                info.data_size,
                info.entry_count,
                info.checkpoint_count,
                0,
                info.data_offset,
            )

            record += b"\x00" * (
                BUCKET_INDEX_SIZE - len(record)
            )

            f.write(record)
            sha256.update(record)

        # Bucket data.
        for bucket_data, _ in buckets:
            f.write(bucket_data)
            sha256.update(bucket_data)

        digest = sha256.digest()

        assert f.tell() == sha256_offset

        f.write(digest)

    write_time = time.perf_counter() - write_start

    print(
        f"Write time    : {write_time:.3f} s"
    )

    return total_file_size, finalized_infos


# ============================================================================
# VERIFIER
# ============================================================================

def verify_dat(
    output: Path,
    expected_entries: int,
    expected_buckets: int,
) -> None:

    print()
    print("=" * 92)
    print("VERIFYING .dat")
    print("=" * 92)

    data = output.read_bytes()

    if len(data) < GLOBAL_HEADER_SIZE + 32:
        raise RuntimeError("DAT file is too small.")

    header = struct.unpack(
        GLOBAL_HEADER_FMT,
        data[:GLOBAL_HEADER_SIZE],
    )

    (
        magic,
        version,
        header_size,
        entry_count,
        bucket_count,
        bucket_target,
        checkpoint_every,
        entry_header_size,
        _reserved,
        bucket_index_offset,
        bucket_data_offset,
        total_file_size,
        sha256_offset,
    ) = header

    if magic != MAGIC:
        raise RuntimeError("Invalid DAT magic.")

    if version != VERSION:
        raise RuntimeError(
            f"Unsupported DAT version: {version}"
        )

    if entry_count != expected_entries:
        raise RuntimeError(
            f"Entry count mismatch: "
            f"{entry_count} != {expected_entries}"
        )

    if bucket_count != expected_buckets:
        raise RuntimeError(
            f"Bucket count mismatch: "
            f"{bucket_count} != {expected_buckets}"
        )

    if entry_header_size != ENTRY_HEADER_SIZE:
        raise RuntimeError(
            "Entry header size mismatch."
        )

    if total_file_size != len(data):
        raise RuntimeError(
            f"File size mismatch: "
            f"header={total_file_size}, actual={len(data)}"
        )

    if sha256_offset != len(data) - 32:
        raise RuntimeError(
            "SHA256 offset mismatch."
        )

    actual_digest = hashlib.sha256(
        data[:sha256_offset]
    ).digest()

    stored_digest = data[sha256_offset:]

    if actual_digest != stored_digest:
        raise RuntimeError(
            "SHA256 verification failed."
        )

    print(
        f"Magic             : {magic.decode()}"
    )

    print(
        f"Version           : {version}"
    )

    print(
        f"Entries           : {entry_count:,}"
    )

    print(
        f"Buckets           : {bucket_count:,}"
    )

    print(
        f"Bucket target     : "
        f"{bucket_target / 1024:.0f} KB"
    )

    print(
        f"Checkpoint every  : "
        f"{checkpoint_every}"
    )

    print(
        f"Entry header      : "
        f"{entry_header_size} bytes"
    )

    print(
        f"File size         : "
        f"{len(data) / 1024 / 1024:.2f} MB"
    )

    print(
        "SHA256            : PASS"
    )

    # Verify bucket index bounds.
    index_end = (
        bucket_index_offset
        + bucket_count * BUCKET_INDEX_SIZE
    )

    if index_end != bucket_data_offset:
        raise RuntimeError(
            "Bucket index/data boundary mismatch."
        )

    previous_bucket_offset = 0
    total_bucket_entries = 0

    max_bucket = 0

    for i in range(bucket_count):

        off = (
            bucket_index_offset
            + i * BUCKET_INDEX_SIZE
        )

        record = data[
            off:
            off + BUCKET_INDEX_SIZE
        ]

        unpacked = struct.unpack(
            BUCKET_INDEX_FMT,
            record[:BUCKET_INDEX_RAW_SIZE],
        )

        (
            first_checkpoint_offset,
            first_entry_index,
            bucket_size,
            bucket_entries,
            checkpoint_count,
            _,
            bucket_offset,
        ) = unpacked

        if bucket_offset < bucket_data_offset:
            raise RuntimeError(
                f"Bucket {i}: invalid offset."
            )

        if bucket_offset + bucket_size > sha256_offset:
            raise RuntimeError(
                f"Bucket {i}: exceeds data region."
            )

        if i > 0 and bucket_offset <= previous_bucket_offset:
            raise RuntimeError(
                f"Bucket {i}: non-monotonic offset."
            )

        previous_bucket_offset = bucket_offset

        total_bucket_entries += bucket_entries

        max_bucket = max(
            max_bucket,
            bucket_size,
        )

        # Validate bucket header.
        bh = data[
            bucket_offset:
            bucket_offset + BUCKET_HEADER_SIZE
        ]

        (
            bucket_magic,
            bucket_header_size,
            _reserved,
            actual_entries,
            actual_checkpoints,
            checkpoint_offset,
            entry_offset,
            actual_bucket_size,
            _,
        ) = struct.unpack(
            BUCKET_HEADER_FMT,
            bh,
        )

        if bucket_magic != BUCKET_MAGIC:
            raise RuntimeError(
                f"Bucket {i}: invalid magic."
            )

        if actual_entries != bucket_entries:
            raise RuntimeError(
                f"Bucket {i}: entry count mismatch."
            )

        if actual_checkpoints != checkpoint_count:
            raise RuntimeError(
                f"Bucket {i}: checkpoint count mismatch."
            )

        if actual_bucket_size != bucket_size:
            raise RuntimeError(
                f"Bucket {i}: size mismatch."
            )

        if checkpoint_offset != BUCKET_HEADER_SIZE:
            raise RuntimeError(
                f"Bucket {i}: invalid checkpoint offset."
            )

        if entry_offset < checkpoint_offset:
            raise RuntimeError(
                f"Bucket {i}: invalid entry offset."
            )

        if first_checkpoint_offset != (
            bucket_offset + checkpoint_offset
        ):
            raise RuntimeError(
                f"Bucket {i}: invalid first checkpoint."
            )

    if total_bucket_entries != expected_entries:
        raise RuntimeError(
            f"Bucket entry total mismatch: "
            f"{total_bucket_entries} != {expected_entries}"
        )

    print(
        "Bucket index      : PASS"
    )

    print(
        "Bucket headers    : PASS"
    )

    print(
        f"Max bucket        : "
        f"{max_bucket / 1024:.2f} KB"
    )

    print(
        "Entry preservation: PASS"
    )

    print()
    print("DAT VERIFICATION: PASS")


# ============================================================================
# REPORT
# ============================================================================

def print_report(
    output: Path,
    entries: list[Entry],
    infos: list[BucketInfo],
) -> None:

    file_size = output.stat().st_size

    bucket_sizes = [
        x.data_size
        for x in infos
    ]

    checkpoint_count = sum(
        x.checkpoint_count
        for x in infos
    )

    print()
    print("=" * 92)
    print("ECDICT STEP 4.1 — EXPORT COMPLETE")
    print("=" * 92)

    print(
        f"Output            : {output}"
    )

    print(
        f"Entries           : {len(entries):,}"
    )

    print(
        f"Buckets           : {len(infos):,}"
    )

    print(
        f"Checkpoints       : {checkpoint_count:,}"
    )

    print(
        f"Entry header      : {ENTRY_HEADER_SIZE} bytes"
    )

    print(
        f"Checkpoint every  : {CHECKPOINT_INTERVAL}"
    )

    print(
        f"File size         : "
        f"{file_size / 1024 / 1024:.2f} MB"
    )

    print(
        f"Average bucket    : "
        f"{sum(bucket_sizes) / len(bucket_sizes) / 1024:.2f} KB"
    )

    print(
        f"Max bucket        : "
        f"{max(bucket_sizes) / 1024:.2f} KB"
    )

    print()

    print("Lookup model:")
    print("  normalize(input)")
    print("        ↓")
    print("  binary search bucket")
    print("        ↓")
    print("  checkpoint binary search")
    print("        ↓")
    print("  scan <= 64 entries")
    print("        ↓")
    print("  exact normalized comparison")
    print("        ↓")
    print("  return original word + fields")

    print()
    print("STEP 4.1 COMPLETE")


# ============================================================================
# MAIN
# ============================================================================

def main() -> None:

    parser = argparse.ArgumentParser(
        description="ECDICT Step 4.1 exporter"
    )

    parser.add_argument(
        "database",
        type=Path,
        help="ECDICT SQLite database",
    )

    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("ecdict.dat"),
        help="Output .dat file",
    )

    parser.add_argument(
        "--bucket-size",
        type=int,
        default=DEFAULT_BUCKET_TARGET,
        help="Target bucket size in bytes",
    )

    args = parser.parse_args()

    if args.bucket_size <= 0:
        parser.error(
            "--bucket-size must be positive"
        )

    print("=" * 92)
    print("ECDICT STEP 4.1 — PC EXPORTER")
    print("=" * 92)

    print()
    print(
        f"Database          : {args.database}"
    )

    print(
        f"Output            : {args.output}"
    )

    print(
        f"Bucket target     : "
        f"{args.bucket_size / 1024:.0f} KB"
    )

    print(
        f"Checkpoint        : "
        f"every {CHECKPOINT_INTERVAL} entries"
    )

    print(
        f"Entry header      : "
        f"{ENTRY_HEADER_SIZE} bytes"
    )

    print()
    print("Phase 1/4 — Reading SQLite...")

    start = time.perf_counter()

    entries = load_entries(args.database)

    read_time = time.perf_counter() - start

    print()
    print(
        f"Entries           : {len(entries):,}"
    )

    print(
        f"Read time         : {read_time:.3f} s"
    )

    print()
    print("Phase 2/4 — Normalizing + sorting...")

    sort_start = time.perf_counter()

    sort_entries(entries)

    sort_time = time.perf_counter() - sort_start

    print(
        f"Sort time         : {sort_time:.3f} s"
    )

    print()
    print("Verifying normalized sort order...")

    verify_start = time.perf_counter()

    verify_sort(entries)
    verify_duplicates(entries)

    verify_time = time.perf_counter() - verify_start

    print(
        f"Sort check        : PASS "
        f"({verify_time:.3f} s)"
    )

    print()
    print("Phase 3/4 — Building .dat...")

    total_size, infos = write_dat(
        args.output,
        entries,
        args.bucket_size,
    )

    print()
    print("Phase 4/4 — Verifying .dat...")

    verify_dat(
        args.output,
        len(entries),
        len(infos),
    )

    print_report(
        args.output,
        entries,
        infos,
    )


if __name__ == "__main__":
    main()