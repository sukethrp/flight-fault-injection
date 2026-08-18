#!/usr/bin/env python3
"""Overlaid wake-error histogram. Log x and log y. Legend from CSV # metadata."""

import argparse
import os
import sys

import matplotlib.pyplot as plt
import numpy as np

sys.path.insert(0, os.path.dirname(__file__))

from percentiles import parse_csv, display_name, warn_if_denied


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csvs", nargs="+")
    p.add_argument("--out", required=True)
    args = p.parse_args()

    fig, ax = plt.subplots()
    for path in args.csvs:
        meta, wake_us = parse_csv(path)
        name = display_name(meta, path)
        warn_if_denied(path, meta, name)
        n = wake_us.size
        legend = f"{name}, n={n}"
        lo = max(1.0, float(np.floor(wake_us.min())))
        hi = float(wake_us.max())
        if hi <= lo:
            hi = lo + 1.0
        bins = np.logspace(np.log10(lo), np.log10(hi), 80)
        ax.hist(
            np.maximum(wake_us, lo),
            bins=bins,
            histtype="step",
            density=False,
            label=legend,
        )

    ax.set_xscale("log")
    ax.set_yscale("log")
    ax.set_xlabel("wake error (µs)")
    ax.set_ylabel("count")
    ax.legend()
    fig.text(
        0.5,
        0.01,
        "log x starts at 1 µs; smaller samples sit in the first bin",
        ha="center",
    )
    fig.tight_layout(rect=(0, 0.04, 1, 1))
    fig.savefig(args.out, dpi=150)
    plt.close(fig)


if __name__ == "__main__":
    main()
