#!/usr/bin/env python3
"""Checks only stdio framing. It is not evidence that Chromium connectNative works."""
import importlib.util
import io
import pathlib

host_path = pathlib.Path(__file__).parents[1] / "native-host" / "marta_browser_host.py"
spec = importlib.util.spec_from_file_location("host", host_path)
host = importlib.util.module_from_spec(spec)
spec.loader.exec_module(host)
stream = io.BytesIO()
host.write_message(stream, {"id": "smoke", "method": "smoke.echo", "params": {"value": "smoke"}})
stream.seek(0)
assert host.read_message(stream)["id"] == "smoke"
print("Native framing OK (no Chromium connectNative check performed)")
