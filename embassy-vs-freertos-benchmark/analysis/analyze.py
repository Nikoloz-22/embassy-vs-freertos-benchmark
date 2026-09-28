#!/usr/bin/env python3
"""Reproduce summary statistics and figures for the STM32F411 benchmark."""
from pathlib import Path
import csv, math, statistics

ROOT = Path(__file__).resolve().parents[1]
DATA = ROOT / "data"
FIG = ROOT / "figures"
FIG.mkdir(exist_ok=True)
HCLK_HZ = 16_000_000


def load(name):
    with (DATA / name).open(newline="", encoding="utf-8") as f:
        rows = list(csv.DictReader(f))
    if len(rows) != 1000:
        raise ValueError(f"{name}: expected 1000 samples, got {len(rows)}")
    return {k: [int(r[k]) for r in rows] for k in ("empty", "crc", "roundtrip")}


def percentile(values, p):
    x = sorted(values)
    pos = (len(x) - 1) * p
    lo, hi = math.floor(pos), math.ceil(pos)
    if lo == hi:
        return float(x[lo])
    return x[lo] * (hi - pos) + x[hi] * (pos - lo)


def stats(values):
    return {
        "mean": statistics.fmean(values),
        "median": statistics.median(values),
        "stdev": statistics.stdev(values),
        "min": min(values),
        "p95": percentile(values, .95),
        "p99": percentile(values, .99),
        "max": max(values),
    }


def us(cycles):
    return cycles / HCLK_HZ * 1_000_000

rust = load("rust.csv")
free = load("freertos.csv")

print("STM32F411CEU6 benchmark, HCLK = 16 MHz, n = 1000 per implementation")
print()
for label, ds in [("Rust/Embassy", rust), ("C/FreeRTOS", free)]:
    print(label)
    for metric in ("empty", "crc", "roundtrip"):
        s = stats(ds[metric])
        print(f"  {metric:9s}: median={s['median']:.2f}, mean={s['mean']:.2f}, "
              f"sd={s['stdev']:.2f}, min={s['min']}, p95={s['p95']:.2f}, "
              f"p99={s['p99']:.2f}, max={s['max']}")
    print()

rmed = stats(rust["roundtrip"])["median"]
fmed = stats(free["roundtrip"])["median"]
print(f"Median round-trip: Rust {rmed:.0f} cycles ({us(rmed):.2f} us), "
      f"FreeRTOS {fmed:.0f} cycles ({us(fmed):.2f} us)")
print(f"FreeRTOS/Rust median ratio: {fmed/rmed:.3f}x")
print(f"Rust median reduction vs FreeRTOS: {(fmed-rmed)/fmed*100:.2f}%")

# Figures are optional so the statistics still work without matplotlib.
try:
    import matplotlib.pyplot as plt
except ImportError:
    print("\nmatplotlib not installed; skipping figures. Run: pip install -r requirements.txt")
    raise SystemExit(0)

plt.figure(figsize=(7.2, 4.2))
plt.plot(range(1000), rust["roundtrip"], label="Rust/Embassy", linewidth=1.0)
plt.plot(range(1000), free["roundtrip"], label="C/FreeRTOS", linewidth=1.0)
plt.xlabel("Sample")
plt.ylabel("Round-trip latency (cycles)")
plt.title("Round-trip latency across 1,000 samples")
plt.legend()
plt.tight_layout()
plt.savefig(FIG / "roundtrip_series.png", dpi=200)
plt.close()

plt.figure(figsize=(6.4, 4.2))
plt.boxplot([rust["roundtrip"], free["roundtrip"]], tick_labels=["Rust/Embassy", "C/FreeRTOS"], showfliers=True)
plt.ylabel("Round-trip latency (cycles)")
plt.title("Round-trip latency distribution")
plt.tight_layout()
plt.savefig(FIG / "roundtrip_boxplot.png", dpi=200)
plt.close()

print(f"Figures written to {FIG}")
