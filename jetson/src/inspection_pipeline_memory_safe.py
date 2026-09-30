#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import argparse
import subprocess
import sys
import time
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path("/home/nvideo/domino_inspection")
PYTHON = sys.executable

CAPTURE_SCRIPT = PROJECT_ROOT / "src" / "capture_experiment.py"
ANALYZE_SCRIPT = PROJECT_ROOT / "src" / "inspection_analyze_sample.py"
WAIT_REMOVE_SCRIPT = PROJECT_ROOT / "src" / "wait_remove_worker.py"

EXPERIMENT = "production"
RETRY_DELAY_SECONDS = 3
MODEL_SWITCH_DELAY_SECONDS = 2


def safe_sample_name():
    return "inspection_" + datetime.now().strftime("%Y%m%d_%H%M%S_%f")


def run_stage(command, stage_name):
    print()
    print("=" * 72)
    print("STAGE:", stage_name)
    print("COMMAND:", " ".join(str(x) for x in command))
    print("=" * 72)

    completed = subprocess.run(command)

    if completed.returncode != 0:
        raise RuntimeError(
            "%s failed with exit code %d"
            % (stage_name, completed.returncode)
        )


def inspect_once(send_enabled=True):
    sample = safe_sample_name()

    run_stage(
        [PYTHON, str(CAPTURE_SCRIPT), EXPERIMENT, sample],
        "YOLO_CAPTURE",
    )

    print()
    print("YOLO process finished.")
    print("Waiting %d s before DINO..." % MODEL_SWITCH_DELAY_SECONDS)
    time.sleep(MODEL_SWITCH_DELAY_SECONDS)

    command = [
        PYTHON,
        str(ANALYZE_SCRIPT),
        EXPERIMENT,
        sample,
    ]

    if send_enabled:
        command.append("--send")

    run_stage(command, "DINO_ANALYZE")

    print()
    print("DINO process finished.")
    print("Inspection sample:", sample)


def wait_remove():
    print()
    print("Waiting %d s before restarting YOLO..." % MODEL_SWITCH_DELAY_SECONDS)
    time.sleep(MODEL_SWITCH_DELAY_SECONDS)

    run_stage(
        [PYTHON, str(WAIT_REMOVE_SCRIPT)],
        "WAIT_REMOVE",
    )


def parse_args():
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--once",
        action="store_true",
        help="Inspect one object and exit. Any error also exits immediately.",
    )

    parser.add_argument(
        "--no-send",
        action="store_true",
        help="Create result locally but do not send it to Ubuntu.",
    )

    return parser.parse_args()


def main():
    args = parse_args()

    print("JETSON MEMORY-SAFE INSPECTION PIPELINE")
    print("Project:", PROJECT_ROOT)
    print("Send enabled:", not args.no_send)
    print("Once mode:", args.once)
    print()
    print("YOLO and DINO are intentionally never loaded in the same process.")

    while True:
        try:
            inspect_once(send_enabled=not args.no_send)

            if args.once:
                print("ONCE mode complete. Exiting.")
                return 0

            wait_remove()

        except KeyboardInterrupt:
            print()
            print("Stopped by user.")
            return 130

        except Exception as error:
            print()
            print("PIPELINE ERROR:", repr(error))

            if args.once:
                print("ONCE mode: error occurred, so pipeline will NOT retry.")
                print("Exiting now.")
                return 1

            print(
                "No model is kept resident by the controller. "
                "Retrying in %d s..." % RETRY_DELAY_SECONDS
            )
            time.sleep(RETRY_DELAY_SECONDS)


if __name__ == "__main__":
    sys.exit(main())
