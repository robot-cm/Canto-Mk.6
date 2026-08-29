#!/usr/bin/env python3

import argparse
import json
import math
import os
import sqlite3
import statistics
from datetime import datetime
from pathlib import Path


def quote_ident(name: str) -> str:
    return '"' + name.replace('"', '""') + '"'


def json_safe(value):
    if value is None:
        return None
    if isinstance(value, bytes):
        return f"<BLOB {len(value)} bytes>"
    return value


def percentile(values, p):
    if not values:
        return None
    values = sorted(values)
    k = (len(values) - 1) * p
    f = math.floor(k)
    c = math.ceil(k)
    if f == c:
        return values[f]
    return values[f] + (values[c] - values[f]) * (k - f)


def get_db_info(conn, db_path):
    stat = os.stat(db_path)

    page_size = conn.execute("PRAGMA page_size").fetchone()[0]
    page_count = conn.execute("PRAGMA page_count").fetchone()[0]
    freelist_count = conn.execute("PRAGMA freelist_count").fetchone()[0]
    journal_mode = conn.execute("PRAGMA journal_mode").fetchone()[0]
    encoding = conn.execute("PRAGMA encoding").fetchone()[0]
    user_version = conn.execute("PRAGMA user_version").fetchone()[0]

    return {
        "path": str(db_path.resolve()),
        "file_size_bytes": stat.st_size,
        "file_size_mb": round(stat.st_size / 1024 / 1024, 3),
        "sqlite_page_size": page_size,
        "sqlite_page_count": page_count,
        "sqlite_freelist_count": freelist_count,
        "journal_mode": journal_mode,
        "encoding": encoding,
        "user_version": user_version,
    }


def get_tables(conn):
    rows = conn.execute("""
        SELECT name, type, sql
        FROM sqlite_master
        WHERE type IN ('table', 'view')
        ORDER BY type, name
    """).fetchall()

    return [
        {
            "name": row[0],
            "type": row[1],
            "sql": row[2],
        }
        for row in rows
    ]


def get_columns(conn, table):
    rows = conn.execute(
        f"PRAGMA table_info({quote_ident(table)})"
    ).fetchall()

    result = []

    for row in rows:
        # cid, name, type, notnull, dflt_value, pk
        result.append({
            "cid": row[0],
            "name": row[1],
            "type": row[2],
            "not_null": bool(row[3]),
            "default": row[4],
            "primary_key_position": row[5],
        })

    return result


def get_indexes(conn, table):
    rows = conn.execute(
        f"PRAGMA index_list({quote_ident(table)})"
    ).fetchall()

    indexes = []

    for row in rows:
        # seq, name, unique, origin, partial
        index_name = row[1]

        index_info = conn.execute(
            f"PRAGMA index_info({quote_ident(index_name)})"
        ).fetchall()

        indexes.append({
            "name": index_name,
            "unique": bool(row[2]),
            "origin": row[3],
            "partial": bool(row[4]),
            "columns": [
                {
                    "sequence": x[0],
                    "column_id": x[1],
                    "column_name": x[2],
                }
                for x in index_info
            ],
        })

    return indexes


def get_foreign_keys(conn, table):
    rows = conn.execute(
        f"PRAGMA foreign_key_list({quote_ident(table)})"
    ).fetchall()

    return [
        {
            "id": row[0],
            "sequence": row[1],
            "table": row[2],
            "from": row[3],
            "to": row[4],
            "on_update": row[5],
            "on_delete": row[6],
            "match": row[7],
        }
        for row in rows
    ]


def get_row_count(conn, table):
    return conn.execute(
        f"SELECT COUNT(*) FROM {quote_ident(table)}"
    ).fetchone()[0]


def analyze_column(conn, table, column):
    qtable = quote_ident(table)
    qcol = quote_ident(column)

    total = conn.execute(
        f"SELECT COUNT(*) FROM {qtable}"
    ).fetchone()[0]

    null_count = conn.execute(
        f"SELECT COUNT(*) FROM {qtable} WHERE {qcol} IS NULL"
    ).fetchone()[0]

    empty_count = conn.execute(
        f"""
        SELECT COUNT(*)
        FROM {qtable}
        WHERE {qcol} IS NOT NULL
          AND typeof({qcol}) = 'text'
          AND {qcol} = ''
        """
    ).fetchone()[0]

    type_rows = conn.execute(
        f"""
        SELECT typeof({qcol}), COUNT(*)
        FROM {qtable}
        GROUP BY typeof({qcol})
        ORDER BY COUNT(*) DESC
        """
    ).fetchall()

    result = {
        "null_count": null_count,
        "null_ratio": round(null_count / total, 6) if total else None,
        "empty_string_count": empty_count,
        "empty_string_ratio": round(empty_count / total, 6) if total else None,
        "sqlite_types": {
            row[0]: row[1]
            for row in type_rows
        },
    }

    # 文本长度统计
    text_count = conn.execute(
        f"""
        SELECT COUNT(*)
        FROM {qtable}
        WHERE {qcol} IS NOT NULL
          AND typeof({qcol}) = 'text'
        """
    ).fetchone()[0]

    if text_count:
        stats = conn.execute(
            f"""
            SELECT
                MIN(LENGTH({qcol})),
                MAX(LENGTH({qcol})),
                AVG(LENGTH({qcol}))
            FROM {qtable}
            WHERE {qcol} IS NOT NULL
              AND typeof({qcol}) = 'text'
            """
        ).fetchone()

        result["text_length"] = {
            "count": text_count,
            "min": stats[0],
            "max": stats[1],
            "average": round(stats[2], 3),
        }

        # 百分位需要读取长度，但为了避免大数据集造成过大内存，
        # 只在字段规模 <= 2,000,000 时执行。
        if text_count <= 2_000_000:
            lengths = [
                row[0]
                for row in conn.execute(
                    f"""
                    SELECT LENGTH({qcol})
                    FROM {qtable}
                    WHERE {qcol} IS NOT NULL
                      AND typeof({qcol}) = 'text'
                    """
                )
            ]

            result["text_length"]["p50"] = percentile(lengths, 0.50)
            result["text_length"]["p90"] = percentile(lengths, 0.90)
            result["text_length"]["p95"] = percentile(lengths, 0.95)
            result["text_length"]["p99"] = percentile(lengths, 0.99)

    return result


def get_samples(conn, table, limit=10):
    rows = conn.execute(
        f"SELECT * FROM {quote_ident(table)} LIMIT {limit}"
    )

    columns = [description[0] for description in rows.description]

    result = []

    for row in rows:
        result.append({
            columns[i]: json_safe(row[i])
            for i in range(len(columns))
        })

    return result


def analyze_table(conn, table_info, samples):
    table = table_info["name"]

    if table_info["type"] != "table":
        return {
            **table_info,
            "columns": get_columns(conn, table),
            "indexes": [],
            "foreign_keys": [],
        }

    columns = get_columns(conn, table)

    result = {
        **table_info,
        "row_count": get_row_count(conn, table),
        "columns": columns,
        "indexes": get_indexes(conn, table),
        "foreign_keys": get_foreign_keys(conn, table),
        "column_statistics": {},
        "samples": get_samples(conn, table, samples),
    }

    for column in columns:
        name = column["name"]

        print(f"      analyzing column: {name}")

        result["column_statistics"][name] = analyze_column(
            conn,
            table,
            name,
        )

    return result


def markdown_report(report):
    lines = []

    lines.append("# ECDICT SQLite Structure Report")
    lines.append("")
    lines.append(
        f"Generated: `{report['generated_at']}`"
    )
    lines.append("")

    db = report["database"]

    lines.append("## Database")
    lines.append("")
    lines.append(f"- File: `{db['path']}`")
    lines.append(f"- Size: `{db['file_size_mb']} MB`")
    lines.append(f"- SQLite page size: `{db['sqlite_page_size']}`")
    lines.append(f"- Page count: `{db['sqlite_page_count']}`")
    lines.append(f"- Encoding: `{db['encoding']}`")
    lines.append(f"- Journal mode: `{db['journal_mode']}`")
    lines.append("")

    lines.append("## Tables")
    lines.append("")
    lines.append("| Table | Rows | Columns |")
    lines.append("|---|---:|---:|")

    for table in report["tables"]:
        lines.append(
            f"| `{table['name']}` | "
            f"{table.get('row_count', '-')} | "
            f"{len(table.get('columns', []))} |"
        )

    lines.append("")

    for table in report["tables"]:
        if table["type"] != "table":
            continue

        name = table["name"]

        lines.append(f"## `{name}`")
        lines.append("")

        lines.append(
            f"Rows: **{table.get('row_count', 0):,}**"
        )
        lines.append("")

        lines.append("### Columns")
        lines.append("")
        lines.append(
            "| # | Name | SQLite Type | NOT NULL | PK | NULL % | Avg Length | Max Length |"
        )
        lines.append(
            "|---:|---|---|---|---|---:|---:|---:|"
        )

        for column in table["columns"]:
            name2 = column["name"]
            stat = table["column_statistics"].get(name2, {})

            text_length = stat.get("text_length", {})

            avg_length = text_length.get("average", "")
            max_length = text_length.get("max", "")

            null_ratio = stat.get("null_ratio")

            null_percent = (
                f"{null_ratio * 100:.2f}%"
                if null_ratio is not None
                else ""
            )

            pk = column["primary_key_position"] or ""

            lines.append(
                f"| {column['cid']} | `{name2}` | "
                f"`{column['type']}` | "
                f"{'yes' if column['not_null'] else 'no'} | "
                f"{pk} | "
                f"{null_percent} | "
                f"{avg_length} | "
                f"{max_length} |"
            )

        lines.append("")

        if table["indexes"]:
            lines.append("### Indexes")
            lines.append("")

            for index in table["indexes"]:
                cols = ", ".join(
                    x["column_name"]
                    for x in index["columns"]
                )

                lines.append(
                    f"- `{index['name']}` "
                    f"({'UNIQUE' if index['unique'] else 'non-unique'}): "
                    f"`{cols}`"
                )

            lines.append("")

        if table["foreign_keys"]:
            lines.append("### Foreign Keys")
            lines.append("")

            for fk in table["foreign_keys"]:
                lines.append(
                    f"- `{fk['from']}` → "
                    f"`{fk['table']}.{fk['to']}`"
                )

            lines.append("")

        lines.append("### Sample Rows")
        lines.append("")
        lines.append("```json")
        lines.append(
            json.dumps(
                table["samples"],
                ensure_ascii=False,
                indent=2,
            )
        )
        lines.append("```")
        lines.append("")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Probe an ECDICT SQLite database."
    )

    parser.add_argument(
        "database",
        type=Path,
        help="Path to ECDICT SQLite database",
    )

    parser.add_argument(
        "--output",
        type=Path,
        default=Path("ecdict_probe_report"),
        help="Output directory",
    )

    parser.add_argument(
        "--samples",
        type=int,
        default=10,
        help="Number of sample rows per table",
    )

    args = parser.parse_args()

    db_path = args.database

    if not db_path.exists():
        raise SystemExit(
            f"Database not found: {db_path}"
        )

    args.output.mkdir(
        parents=True,
        exist_ok=True,
    )

    print(f"[+] Database: {db_path}")
    print("[+] Opening SQLite...")

    conn = sqlite3.connect(
        f"file:{db_path.resolve()}?mode=ro",
        uri=True,
    )

    # 只读模式
    conn.execute("PRAGMA query_only = ON")

    report = {
        "generated_at": datetime.now().isoformat(),
        "database": get_db_info(conn, db_path),
        "tables": [],
    }

    print("[+] Database information collected")

    tables = get_tables(conn)

    print(f"[+] Found {len(tables)} tables/views")

    for i, table_info in enumerate(tables, 1):
        name = table_info["name"]

        print(
            f"\n[{i}/{len(tables)}] {name}"
        )

        if table_info["type"] == "table":
            row_count = get_row_count(conn, name)

            print(
                f"    rows: {row_count:,}"
            )

        result = analyze_table(
            conn,
            table_info,
            args.samples,
        )

        report["tables"].append(result)

    conn.close()

    json_path = args.output / "report.json"
    md_path = args.output / "report.md"

    with json_path.open(
        "w",
        encoding="utf-8",
    ) as f:
        json.dump(
            report,
            f,
            ensure_ascii=False,
            indent=2,
        )

    with md_path.open(
        "w",
        encoding="utf-8",
    ) as f:
        f.write(
            markdown_report(report)
        )

    print("\n" + "=" * 60)
    print("DONE")
    print("=" * 60)
    print(f"JSON: {json_path}")
    print(f"Markdown: {md_path}")


if __name__ == "__main__":
    main()