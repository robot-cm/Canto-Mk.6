# ECDICT SQLite Structure Report

Generated: `2026-08-29T19:46:05.622068`

## Database

- File: `/home/erashaperavm/下载/CantoMk6-fork/resources/ecdict-sqlite-28/stardict.db`
- Size: `811.852 MB`
- SQLite page size: `1024`
- Page count: `831336`
- Encoding: `UTF-8`
- Journal mode: `delete`

## Tables

| Table | Rows | Columns |
|---|---:|---:|
| `sqlite_sequence` | 1 | 2 |
| `stardict` | 3402564 | 15 |

## `sqlite_sequence`

Rows: **1**

### Columns

| # | Name | SQLite Type | NOT NULL | PK | NULL % | Avg Length | Max Length |
|---:|---|---|---|---|---:|---:|---:|
| 0 | `name` | `` | no |  | 0.00% | 8.0 | 8 |
| 1 | `seq` | `` | no |  | 0.00% |  |  |

### Sample Rows

```json
[
  {
    "name": "stardict",
    "seq": 3402564
  }
]
```

## `stardict`

Rows: **3,402,564**

### Columns

| # | Name | SQLite Type | NOT NULL | PK | NULL % | Avg Length | Max Length |
|---:|---|---|---|---|---:|---:|---:|
| 0 | `id` | `INTEGER` | yes | 1 | 0.00% |  |  |
| 1 | `word` | `VARCHAR(64)` | yes |  | 0.00% | 15.109 | 147 |
| 2 | `sw` | `VARCHAR(64)` | yes |  | 0.00% | 13.977 | 125 |
| 3 | `phonetic` | `VARCHAR(64)` | no |  | 71.15% | 4.149 | 79 |
| 4 | `definition` | `TEXT` | no |  | 74.64% | 19.72 | 12343 |
| 5 | `translation` | `TEXT` | no |  | 0.43% | 12.514 | 11814 |
| 6 | `pos` | `VARCHAR(16)` | no |  | 74.98% | 0.335 | 25 |
| 7 | `collins` | `INTEGER` | no |  | 99.60% |  |  |
| 8 | `oxford` | `INTEGER` | no |  | 99.90% |  |  |
| 9 | `tag` | `VARCHAR(64)` | no |  | 75.13% | 0.193 | 34 |
| 10 | `bnc` | `INTEGER` | no |  | 75.13% |  |  |
| 11 | `frq` | `INTEGER` | no |  | 75.13% |  |  |
| 12 | `exchange` | `TEXT` | no |  | 71.47% | 6.876 | 114 |
| 13 | `detail` | `TEXT` | no |  | 100.00% | 2.0 | 2 |
| 14 | `audio` | `TEXT` | no |  | 75.13% | 0.0 | 0 |

### Indexes

- `sd_1` (non-unique): `word`
- `stardict_3` (non-unique): `sw, word`
- `stardict_2` (UNIQUE): `word`
- `stardict_1` (UNIQUE): `id`
- `sqlite_autoindex_stardict_2` (UNIQUE): `word`
- `sqlite_autoindex_stardict_1` (UNIQUE): `id`

### Sample Rows

```json
[
  {
    "id": 1,
    "word": "'a",
    "sw": "a",
    "phonetic": "eɪ",
    "definition": null,
    "translation": "na. 一\nn. 英文字母表的第一字母；【乐】A音\nart. 冠以不定冠词主要表示类别\n[网络] 从；按；一个",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 2,
    "word": "'A' game",
    "sw": "agame",
    "phonetic": null,
    "definition": null,
    "translation": "[网络] 游戏；一个游戏；一局",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 3,
    "word": "'Abbāsīyah",
    "sw": "abbāsīyah",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿巴西耶 ( 埃 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 4,
    "word": "'Abd al Kūrī",
    "sw": "abdalkūrī",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜杜勒库里岛 ( 也门 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 5,
    "word": "'Abd al Mājid",
    "sw": "abdalmājid",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜杜勒马吉德 ( 苏丹 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 6,
    "word": "'Abd al Qādir",
    "sw": "abdalqādir",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜杜勒加迪尔 ( 苏丹 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 7,
    "word": "'Abd Allāh",
    "sw": "abdallāh",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜杜拉 ( 苏丹 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 8,
    "word": "'Abd Allāh, Khawr",
    "sw": "abdallāhkhawr",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜杜拉水道 ( 科·伊拉 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 9,
    "word": "'Abd, 'Ayn al",
    "sw": "abdaynal",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜德泉 ( 利、沙特 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  },
  {
    "id": 10,
    "word": "'Abde, Al",
    "sw": "abdeal",
    "phonetic": null,
    "definition": null,
    "translation": "[地名] 阿卜德 ( 黎 )",
    "pos": null,
    "collins": null,
    "oxford": null,
    "tag": null,
    "bnc": null,
    "frq": null,
    "exchange": null,
    "detail": null,
    "audio": null
  }
]
```
