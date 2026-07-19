#!/usr/bin/env python3
"""Compare browser `timestamp,value` output against native offline CSV.

The two CSVs must represent the same 36x36 RGB inputs, initial state and dt.
It reports rather than hides a model-state divergence.
"""
import csv
import math
import sys

if len(sys.argv) != 3:
    raise SystemExit(f"usage: {sys.argv[0]} browser_bvp.csv native_facephys.csv")

def read(path, candidates):
    with open(path, newline="") as handle:
        rows = list(csv.DictReader(handle))
    for candidate in candidates:
        if rows and candidate in rows[0]:
            return [float(row[candidate]) for row in rows]
    raise ValueError(f"none of {candidates} found in {path}")

browser = read(sys.argv[1], ("value", "bvp"))
native = read(sys.argv[2], ("bvp", "value"))
count = min(len(browser), len(native))
if not count:
    raise SystemExit("no comparable rows")
errors = [abs(browser[i] - native[i]) for i in range(count)]
rmse = math.sqrt(sum(error * error for error in errors) / count)
print(f"compared={count} browser_rows={len(browser)} native_rows={len(native)}")
print(f"max_abs_error={max(errors):.9g} rmse={rmse:.9g}")
for index, error in enumerate(errors):
    if error > 1e-4:
        print(f"first_error_over_1e-4 frame={index} browser={browser[index]:.9g} native={native[index]:.9g} error={error:.9g}")
        break
