#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import json
from pathlib import Path

import requests


DEFAULT_API_URL = "http://192.168.0.46:8010/api/v1/inspections"
DEFAULT_TIMEOUT_SECONDS = 60

FIELD_ORDER = [
    "search_full",
    "search_bbox",
    "full",
    "full_bbox",
    "crop",
    "heatmap",
    "overlay",
    "anomalies",
]


def send_inspection(sample_dir, api_url=DEFAULT_API_URL, timeout=DEFAULT_TIMEOUT_SECONDS):
    sample_dir = Path(sample_dir)
    metadata_path = sample_dir / "inspection_result.json"

    if not metadata_path.exists():
        raise RuntimeError("Missing inspection_result.json: %s" % metadata_path)

    with metadata_path.open("r", encoding="utf-8") as f:
        payload = json.load(f)

    artifacts = payload.get("artifacts", {})
    files = {}
    opened = []

    try:
        for field_name in FIELD_ORDER:
            filename = artifacts.get(field_name)
            if not filename:
                continue

            path = sample_dir / filename
            if not path.exists():
                print("skip missing artifact:", path)
                continue

            handle = path.open("rb")
            opened.append(handle)

            suffix = path.suffix.lower()
            if suffix in (".jpg", ".jpeg"):
                content_type = "image/jpeg"
            elif suffix == ".png":
                content_type = "image/png"
            else:
                content_type = "application/octet-stream"

            files[field_name] = (
                path.name,
                handle,
                content_type,
            )

        with metadata_path.open("r", encoding="utf-8") as f:
            metadata_text = f.read()

        print("Sending inspection:", payload.get("inspection_id"))
        print("API:", api_url)
        print("files:", len(files))

        response = requests.post(
            api_url,
            data={"metadata": metadata_text},
            files=files,
            timeout=timeout,
        )

        print("HTTP:", response.status_code)

        try:
            body = response.json()
        except ValueError:
            body = {"raw_response": response.text}

        if response.status_code not in (200, 201):
            raise RuntimeError(
                "Inspection upload failed: HTTP %s: %s"
                % (response.status_code, body)
            )

        print(json.dumps(body, ensure_ascii=False, indent=2))
        return body

    finally:
        for handle in opened:
            handle.close()


if __name__ == "__main__":
    SAMPLE_DIR = Path(
        "/home/nvideo/domino_inspection/results/experiment_01/defect_test"
    )
    send_inspection(SAMPLE_DIR)
