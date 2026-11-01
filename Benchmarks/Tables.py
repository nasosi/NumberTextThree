"""Turns the CSV that `Bench --csv` prints into Markdown tables, one per operation.

    python Benchmarks/Tables.py results.csv

Each cell holds the minimum nanoseconds per number, and the fastest library in each row is shown in bold.
"""

import csv
import sys


def main(path):
    tables = {}
    for row in csv.DictReader(open(path, newline="")):
        datasets = tables.setdefault(row["operation"], {})
        datasets.setdefault(row["dataset"], {})[row["library"]] = float(row["min_ns"])

    for operation, datasets in tables.items():
        libraries = list(next(iter(datasets.values())))
        print("| " + operation + " | " + " | ".join(libraries) + " |")
        print("|---" * (len(libraries) + 1) + "|")
        for dataset, times in datasets.items():
            fastest = min(times.values())
            cells = []
            for library in libraries:
                if library not in times:
                    cells.append("")
                elif times[library] == fastest:
                    cells.append("**%.1f**" % times[library])
                else:
                    cells.append("%.1f" % times[library])
            print("| " + dataset + " | " + " | ".join(cells) + " |")
        print()


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: python Tables.py results.csv")
    main(sys.argv[1])
