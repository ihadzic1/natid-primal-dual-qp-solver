#!/usr/bin/env python3
"""Plot NatIDQP convergence history."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Plot primal, dual, and complementarity convergence."
    )
    parser.add_argument("csv_file", type=Path, help="CSV produced by natid_qp --csv")
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path("convergence.png"),
        help="Output image (default: convergence.png)",
    )
    parser.add_argument(
        "--title",
        default="NatIDQP convergence",
        help="Plot title",
    )
    return parser.parse_args()


def load_history(file_name: Path) -> dict[str, list[float]]:
    required = {
        "iteration",
        "primal_residual",
        "dual_residual",
        "mu",
    }
    columns: dict[str, list[float]] = {name: [] for name in required}

    with file_name.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        missing = required.difference(reader.fieldnames or [])
        if missing:
            raise ValueError(f"CSV is missing columns: {sorted(missing)}")

        for row in reader:
            for name in required:
                columns[name].append(float(row[name]))

    if not columns["iteration"]:
        raise ValueError("CSV contains no iterations.")
    return columns


def main() -> None:
    args = parse_args()

    try:
        import matplotlib.pyplot as plt
    except ImportError as error:
        raise SystemExit(
            "matplotlib is required: python -m pip install matplotlib"
        ) from error

    history = load_history(args.csv_file)
    iterations = history["iteration"]
    numerical_floor = 1e-18

    figure, axis = plt.subplots(figsize=(8.5, 5.2))
    axis.semilogy(
        iterations,
        [max(value, numerical_floor) for value in history["primal_residual"]],
        marker="o",
        markersize=3,
        label="Primal residual",
    )
    axis.semilogy(
        iterations,
        [max(value, numerical_floor) for value in history["dual_residual"]],
        marker="s",
        markersize=3,
        label="Dual residual",
    )
    axis.semilogy(
        iterations,
        [max(value, numerical_floor) for value in history["mu"]],
        marker="^",
        markersize=3,
        label="Complementarity mu",
    )

    axis.set_title(args.title)
    axis.set_xlabel("Iteration")
    axis.set_ylabel("Residual / complementarity")
    axis.grid(True, which="both", alpha=0.3)
    axis.legend()
    figure.tight_layout()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(args.output, dpi=180)
    print(f"Saved {args.output}")


if __name__ == "__main__":
    main()
