"""Config/codegen tests: run the ESPHome CLI on small generated configs.

Each test writes a YAML file into its own tmp dir that loads the components
from this checkout, then runs `esphome config` (schema only) or
`esphome compile --only-generate` (writes main.cpp and copies the component
sources, no C++ build). Every run is a separate process because the
components keep module-level state (selected drivers / radio types).

Run with the ESPHome venv's Python, e.g.
    ~/esphome-venv/bin/python -m pytest tests/codegen
Add --full to also run the real firmware builds (test_full_compile.py).
"""

import subprocess
import sys
import textwrap
from dataclasses import dataclass
from pathlib import Path

import pytest

COMPONENTS = Path(__file__).resolve().parents[2] / "components"

HEADER = f"""\
esphome:
  name: wmbus-test
esp32:
  board: heltec_wifi_lora_32_V3
  flash_size: 16MB
  framework:
    type: esp-idf
logger:
wifi:
  ssid: test
  password: testtest
external_components:
  - source:
      type: local
      path: {COMPONENTS}
spi:
  clk_pin: GPIO9
  mosi_pin: GPIO10
  miso_pin: GPIO11
"""

TIME = """\
time:
  - platform: sntp
"""

SX1262 = """\
wmbus_radio:
  radio_type: SX1262
  cs_pin: GPIO8
  reset_pin: GPIO12
  irq_pin: GPIO14
  busy_pin: GPIO13
  rf_switch: true
"""

IZAR_METER = """\
wmbus_meter:
  - id: water
    meter_id: 0x2124589C
    type: izar
    mode: [T1]
sensor:
  - platform: wmbus_meter
    parent_id: water
    field: total_m3
    name: Water total
"""


def pytest_addoption(parser):
    parser.addoption(
        "--full",
        action="store_true",
        help="also run full ESP-IDF builds (slow, a few minutes each)",
    )


def pytest_collection_modifyitems(config, items):
    if config.getoption("--full"):
        return
    skip = pytest.mark.skip(reason="full build: run with --full")
    for item in items:
        if "full" in item.keywords:
            item.add_marker(skip)


def pytest_configure(config):
    config.addinivalue_line("markers", "full: real firmware build (needs --full)")


@dataclass
class Result:
    returncode: int
    output: str
    build: Path

    @property
    def ok(self):
        return self.returncode == 0

    @property
    def main_cpp(self):
        return (self.build / "src" / "main.cpp").read_text()

    def component_files(self, component):
        folder = self.build / "src" / "esphome" / "components" / component
        return sorted(p.name for p in folder.iterdir()) if folder.is_dir() else []


@pytest.fixture
def esphome(tmp_path):
    """esphome(yaml, generate=False, mode=None) -> Result.

    mode: "config" (default), "generate" (same as generate=True) or
    "compile" (full ESP-IDF build, minutes).
    """

    def run(yaml, generate=False, mode=None):
        mode = mode or ("generate" if generate else "config")
        config = tmp_path / "wmbus-test.yaml"
        config.write_text(HEADER + textwrap.dedent(yaml))
        cmd = [sys.executable, "-m", "esphome"]
        cmd += {
            "config": ["config"],
            "generate": ["compile", "--only-generate"],
            "compile": ["compile"],
        }[mode]
        proc = subprocess.run(
            cmd + [str(config)],
            capture_output=True,
            text=True,
            timeout=900 if mode == "compile" else 300,
        )
        build = tmp_path / ".esphome" / "build" / "wmbus-test"
        return Result(proc.returncode, proc.stdout + proc.stderr, build)

    return run
