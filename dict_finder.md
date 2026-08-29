# ECDICT ESP32 Lookup Specification

## Step 4.2 — `.dat` Binary Reader / Exact Lookup

> **Status:** Implementation Specification
> **Target:** ESP32 + SD Card
> **Dictionary:** ECDICT SQLite → `ecdict.dat`
> **Lookup:** Exact match only
> **Compression:** None
> **PSRAM:** Not required for the whole dictionary
> **Storage:** Single `.dat` file on SD card

---

# 1. Project Objective

Implement a lightweight C/C++ read-only dictionary lookup library for ESP32.

The dictionary is stored as:

```text
SD Card
└── ecdict.dat
```

Current exported file:

```text
File size       : 208.63 MB
Entries         : 3,402,564
Buckets         : 1,670
Checkpoints     : 53,988
Bucket target   : 128 KB
Checkpoint      : every 64 entries
Entry header    : 12 bytes
Compression     : none
```

The ESP32 must **NOT load the entire dictionary into RAM or PSRAM**.

The `.dat` file must be accessed using random file reads / seeks.

---

# 2. Supported Lookup

The library supports:

```text
exact lookup only
```

No fuzzy search.

No prefix search.

No substring search.

No spelling correction.

No Levenshtein distance.

No ranking.

No suggestions.

No search-as-you-type.

Input is normalized before lookup.

---

# 3. Normalization

The PC exporter uses:

```text
Unicode NFKC
    ↓
strip surrounding whitespace
    ↓
lowercase
```

Conceptually:

```text
normalize(input):
    NFKC(input)
    trim(input)
    lowercase(input)
```

The ESP32 implementation must use the **same normalization semantics** as the exporter.

The normalized string is used for lookup.

---

# 4. Important Case-Sensitivity Rule

The dictionary contains one normalized lookup key per entry:

```text
1 normalized word = 1 entry
```

The lookup key is normalized lowercase.

Therefore:

```text
China
china
CHINA
```

all normalize to:

```text
china
```

This is intentional.

The dictionary is **not case-sensitive**.

The original `word` field is still preserved and returned to the caller.

Example:

```text
Input:

China

Normalized lookup key:

china

Returned word:

China
```

If the dictionary contains:

```text
China
```

the result can preserve:

```text
word = "China"
```

while lookup is performed using:

```text
china
```

Do NOT create separate entries for different capitalization.

---

# 5. Fields Returned

The ESP32 lookup API must expose these fields:

```text
word
phonetic
translation
definition
pos
exchange
```

The `tag` field is intentionally excluded.

Do not implement or store `tag`.

---

# 6. Dictionary Entry Model

Conceptually each entry is:

```text
Entry
├── word
├── phonetic
├── translation
├── definition
├── pos
└── exchange
```

The entry is stored in the `.dat` file using:

```text
12-byte entry header
+
variable-length field payload
```

The exact byte-level definition of the 12-byte header MUST match the PC exporter.

Do NOT redesign the entry format on the ESP32 side.

---

# 7. File Architecture

The dictionary is one file:

```text
ecdict.dat
```

It is NOT a directory containing thousands of files.

Conceptual structure:

```text
+-----------------------------+
| File Header                 |
+-----------------------------+
| Bucket Index                |
+-----------------------------+
| Bucket 0                    |
|   Bucket metadata           |
|   Checkpoints               |
|   Entries                   |
+-----------------------------+
| Bucket 1                    |
|   Bucket metadata           |
|   Checkpoints               |
|   Entries                   |
+-----------------------------+
| ...                         |
+-----------------------------+
| Bucket N                    |
+-----------------------------+
```

The ESP32 accesses only the required portions.

---

# 8. Random Access

The ESP32 must use SD-card random access.

Conceptually:

```cpp
file.seek(offset);
file.read(buffer, size);
```

The entire file must never be loaded into memory.

For example, a lookup should conceptually perform:

```text
normalize(input)

        ↓

locate bucket

        ↓

locate checkpoint

        ↓

seek() to checkpoint

        ↓

read entry

        ↓

compare normalized word

        ↓

continue until match / NOT FOUND
```

---

# 9. Bucket Design

Current exporter configuration:

```text
Target bucket size = 128 KB
```

There are:

```text
1,670 buckets
```

The actual bucket size is variable because buckets are determined from entry boundaries.

Exporter verification:

```text
Max bucket = 128.00 KB
Average bucket = 127.88 KB
```

Therefore the ESP32 implementation must NOT assume every bucket is exactly 128 KB.

The bucket index determines the actual location and size.

---

# 10. Checkpoints

Checkpoint interval:

```text
64 entries
```

Therefore:

```text
checkpoint 0
    entries 0..63

checkpoint 1
    entries 64..127

checkpoint 2
    entries 128..191

...
```

The exporter generated:

```text
53,988 checkpoints
```

The checkpoint mechanism exists specifically to avoid scanning an entire bucket.

---

# 11. Lookup Algorithm

Recommended algorithm:

```text
Input string
    ↓
Normalize
    ↓
Find bucket
    ↓
Binary search checkpoints
    ↓
Seek to selected checkpoint
    ↓
Sequentially inspect ≤ 64 entries
    ↓
Compare normalized word
    ↓
MATCH
    ↓
Return fields
```

The intended maximum sequential search region is:

```text
64 entries
```

The ESP32 should NOT linearly scan all ~2,000 entries of a bucket.

---

# 12. Bucket Search

The dictionary entries are sorted by normalized word.

The exporter verified:

```text
Normalized sort order: PASS
```

Therefore the lookup implementation can use binary search.

The bucket index should identify the bucket corresponding to the normalized query.

Do NOT scan all 1,670 buckets sequentially.

---

# 13. Checkpoint Search

Checkpoints are sorted by normalized word / entry order.

After selecting the appropriate bucket:

```text
binary search checkpoints
```

Find the checkpoint whose range contains the query.

Then:

```text
file.seek(checkpoint_offset)
```

and sequentially inspect entries.

---

# 14. Exact Match

The final comparison must be:

```text
normalized_entry_word == normalized_query
```

Not:

```text
starts_with()
contains()
fuzzy_match()
case_sensitive_match()
```

Only exact equality is valid.

Example:

```text
Query:
hello

Match:
hello

No match:
hell
helloing
hello-world
```

Result:

```text
FOUND
```

or:

```text
NOT FOUND
```

---

# 15. Entry Reading

The reader should be streaming-oriented.

Do not allocate large buffers unnecessarily.

Conceptually:

```cpp
struct EcdictEntry {
    String / buffer word;
    String / buffer phonetic;
    String / buffer translation;
    String / buffer definition;
    String / buffer pos;
    String / buffer exchange;
};
```

The implementation should preferably use fixed/reusable buffers where practical.

Avoid allocating and freeing large temporary buffers for every entry.

---

# 16. Large Entries

The dictionary contains some unusually large entries.

Largest observed raw entry:

```text
~32.70 KB combined payload
```

Examples include very large `translation` / `definition` fields.

Therefore the implementation MUST NOT assume:

```text
entry < 1 KB
```

or:

```text
entry < 4 KB
```

The reader must respect the lengths encoded by the entry header.

However, there is no need to preload the whole dictionary or entire bucket into RAM.

---

# 17. PSRAM Strategy

PSRAM should NOT be used to cache the entire dictionary.

The dictionary is:

```text
~208.63 MB
```

while ESP32 memory is much smaller.

Recommended model:

```text
SD Card
    │
    │ random read
    ▼
small reusable RAM/PSRAM buffer
    │
    ▼
parse entry
    │
    ▼
return result
```

PSRAM is optional.

If PSRAM is available, it may be used for a larger reusable read buffer, but the lookup algorithm must work without requiring the whole dictionary in PSRAM.

---

# 18. No Compression

The `.dat` file is intentionally uncompressed.

Do NOT add:

```text
gzip
zstd
lz4
deflate
custom compression
```

The current design prioritizes:

```text
simple random access
+
low CPU overhead
+
simple ESP32 implementation
```

over maximum storage compression.

The SD card has sufficient capacity.

---

# 19. No Future-Proofing

Do NOT add unnecessary features.

This is a fixed dictionary build.

Do not implement:

```text
multiple dictionary versions
dynamic updates
in-place modification
database transactions
compression layers
fuzzy search
prefix search
spell correction
network update
incremental updates
```

The `.dat` file is a static read-only asset.

---

# 20. File Open

The ESP32 library should open:

```text
/ecdict.dat
```

or the configured equivalent path.

Recommended API concept:

```cpp
bool begin(fs::FS& fs, const char* path);
```

Example:

```cpp
Ecdict dict;

if (!dict.begin(SD, "/ecdict.dat")) {
    // initialization failure
}
```

---

# 21. Lookup API

Recommended minimal API:

```cpp
bool lookup(
    const char* query,
    EcdictEntry& result
);
```

Behavior:

```text
FOUND:
    return true
    result contains:
        word
        phonetic
        translation
        definition
        pos
        exchange

NOT FOUND:
    return false
```

No exception-based design is required.

---

# 22. Result Lifetime

The implementation should make result ownership explicit.

Preferred approach:

```cpp
EcdictEntry result;
dict.lookup("hello", result);
```

The library fills `result`.

Do not return pointers into temporary stack buffers.

Do not return pointers to invalidated SD read buffers.

---

# 23. Error Handling

Distinguish at least:

```text
FOUND
NOT_FOUND
IO_ERROR
INVALID_FILE
```

A simple boolean API may be used for the first version, but internally the implementation should be capable of distinguishing:

```text
query does not exist
```

from:

```text
SD card read failure
```

and:

```text
invalid/corrupt .dat
```

---

# 24. Initialization Validation

When opening `ecdict.dat`, verify at minimum:

```text
Magic
Version
Entry count
Bucket count
```

Expected current values:

```text
Magic:
ECDICT01

Version:
1

Entries:
3,402,564

Buckets:
1,670
```

If these values are incompatible, initialization should fail instead of silently performing incorrect lookups.

---

# 25. File Integrity

The PC exporter currently verifies:

```text
SHA256: PASS
Bucket index: PASS
Bucket headers: PASS
Entry preservation: PASS
```

The ESP32 does NOT need to calculate the complete SHA256 every time it boots.

Startup should remain fast.

The PC-generated SHA256 can be used as an external deployment/integrity check.

---

# 26. Endianness

The ESP32 reader MUST use the exact byte order produced by the exporter.

Do not assume a different format.

All multi-byte integers must be decoded explicitly rather than relying on C/C++ struct packing.

For example, prefer:

```cpp
read_u32(...)
```

over:

```cpp
reinterpret_cast<uint32_t*>(buffer)
```

This avoids:

```text
alignment problems
packing problems
endianness ambiguity
```

---

# 27. Struct Packing

Do NOT directly map arbitrary on-disk data onto C structs unless the binary layout is explicitly guaranteed.

Bad:

```cpp
struct Header {
    uint32_t a;
    uint32_t b;
    uint32_t c;
};

file.read((uint8_t*)&header, sizeof(header));
```

Preferred:

```cpp
uint32_t a = read_u32(buffer + 0);
uint32_t b = read_u32(buffer + 4);
uint32_t c = read_u32(buffer + 8);
```

The exact meaning of those 12 bytes must come from the PC exporter specification.

---

# 28. Performance Goal

The design intentionally minimizes SD reads.

Ideal lookup:

```text
1 bucket lookup
+
1 checkpoint binary search
+
small sequential entry reads
```

The expected algorithmic structure is:

```text
O(log B)
+
O(log C)
+
O(64)
```

where:

```text
B = number of buckets
C = checkpoints in selected bucket
```

The actual performance depends heavily on SD card latency and filesystem implementation.

---

# 29. Important SD Card Behavior

A 208 MB file is completely valid for this architecture.

The ESP32 does NOT need to read:

```text
0 → 208 MB
```

for every lookup.

Instead:

```text
seek(offset)
read(...)
```

is used.

Therefore the file behaves more like a random-access binary database than a normal sequential data file.

---

# 30. Recommended Internal Architecture

Suggested implementation:

```text
Ecdict
│
├── begin()
│
├── lookup()
│
├── normalize()
│
├── findBucket()
│
├── findCheckpoint()
│
├── readEntry()
│
├── compareWord()
│
└── read helpers
     ├── read_u16()
     ├── read_u32()
     └── read_bytes()
```

Keep the implementation simple.

---

# 31. Recommended Source Layout

Suggested:

```text
Ecdict/
├── Ecdict.h
├── Ecdict.cpp
└── README.md
```

Example usage:

```cpp
#include "Ecdict.h"

Ecdict dict;

void setup() {

    // initialize SD first

    if (!dict.begin(SD, "/ecdict.dat")) {
        Serial.println("ECDICT init failed");
        return;
    }

    EcdictEntry result;

    if (dict.lookup("hello", result)) {

        Serial.println(result.word);
        Serial.println(result.phonetic);
        Serial.println(result.translation);
        Serial.println(result.definition);
        Serial.println(result.pos);
        Serial.println(result.exchange);

    } else {

        Serial.println("NOT FOUND");
    }
}
```

---

# 32. What Must NOT Be Changed

The ESP32 implementation must NOT independently redesign:

```text
bucket size
checkpoint interval
entry layout
field order
normalization rule
file header
bucket index
```

The PC exporter is the authoritative source of the binary format.

The ESP32 reader must implement the format that already exists.

---

# 33. Current Locked Project Decisions

These decisions have already been made:

| Item                      | Decision                                               |
| ------------------------- | ------------------------------------------------------ |
| Dictionary                | ECDICT                                                 |
| Storage                   | SD card                                                |
| File                      | Single `ecdict.dat`                                    |
| Compression               | None                                                   |
| Lookup                    | Exact only                                             |
| Fuzzy search              | No                                                     |
| Prefix search             | No                                                     |
| Case sensitivity          | Normalized lowercase                                   |
| Unicode normalization     | NFKC                                                   |
| Fields                    | word, phonetic, translation, definition, pos, exchange |
| tag                       | Excluded                                               |
| Entry model               | 1 normalized word = 1 entry                            |
| Bucket target             | 128 KB                                                 |
| Checkpoint                | Every 64 entries                                       |
| Entry header              | 12 bytes                                               |
| Whole dictionary in RAM   | No                                                     |
| Whole dictionary in PSRAM | No                                                     |
| Dictionary updates        | No                                                     |
| Runtime modification      | No                                                     |

---

# 34. Critical Implementation Constraint

Before writing the actual ESP32 parser, inspect the **PC exporter source code that generated this exact `ecdict.dat`**.

The following must be extracted from the exporter:

```text
1. File header byte layout
2. Bucket index byte layout
3. Bucket header byte layout
4. Checkpoint byte layout
5. 12-byte entry header layout
6. Field ordering
7. Integer widths
8. Integer endianness
9. String encoding
10. Offset semantics
11. Length semantics
12. Empty-field representation
```

Do NOT guess these values from the summary output.

If any of these details are unavailable, ask for the PC exporter source before implementing the reader.

---

# 35. Final Lookup Pipeline

The final architecture is:

```text
                  PC
                   │
                   │
            SQLite ECDICT
                   │
                   ▼
             PC Exporter
                   │
                   ▼
              ecdict.dat
             ~208.63 MB
                   │
                   │ copy
                   ▼
                SD Card
                   │
                   │ random access
                   ▼
                ESP32
                   │
                   ▼
              normalize()
                   │
                   ▼
             bucket search
                   │
                   ▼
          checkpoint search
                   │
                   ▼
           ≤64 entry scan
                   │
                   ▼
           exact comparison
              /          \
           FOUND        NOT FOUND
             │
             ▼
       return six fields
```

---

# 36. Implementation Priority

Implement in this order:

```text
1. File open
2. File header parser
3. Bucket index parser
4. Bucket lookup
5. Checkpoint lookup
6. Entry header parser
7. Entry field parser
8. Exact word comparison
9. Result object
10. Error handling
11. Benchmark
```

Do NOT start with UI.

Do NOT start with fuzzy search.

Do NOT add caching until the baseline benchmark exists.

---

# 37. Benchmark Requirements

After the reader works, benchmark:

```text
100 exact lookups
```

using a mixture of:

```text
very common words
medium-frequency words
rare words
NOT FOUND queries
short words
long words
```

Measure:

```text
lookup latency
SD read count
bytes read
success rate
NOT FOUND rate
RAM usage
PSRAM usage
```

The benchmark should distinguish:

```text
cold lookup
warm lookup
```

if filesystem/SD caching affects the results.

---

# 38. Definition of Done

Step 4.2 is complete when:

```text
[ ] ESP32 can open ecdict.dat
[ ] File header validates
[ ] Bucket index validates
[ ] Bucket lookup works
[ ] Checkpoint lookup works
[ ] Entry parser works
[ ] Exact lookup works
[ ] NOT FOUND works
[ ] All six fields can be returned
[ ] No full-file RAM loading
[ ] No compression
[ ] No fuzzy search
[ ] PC/ESP32 results match
[ ] Basic benchmark completed
```

After this, the project can proceed to:

```text
STEP 5 — ESP32/C++ Lookup Library
```

and then:

```text
STEP 6 — SD benchmark / optimization
```

---

# 39. Most Important Instruction for the Implementing AI

**Do not redesign the database.**

The database format has already been selected.

Your job is to implement a **reader**, not another exporter.

If the exact byte layout is unclear, request the PC exporter source code and derive the format from that source.

Do not invent:

```text
new headers
new indexes
new compression
new search algorithms
new normalization rules
new caching layers
```

Keep the implementation minimal, deterministic, and compatible with the existing `ecdict.dat`.
