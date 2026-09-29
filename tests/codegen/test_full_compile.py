"""Real ESP-IDF builds (only with --full): every radio type, and all drivers.

The source filters only build what a config selects, so these are the configs
that together compile every file. A few minutes each; the first run also
downloads the toolchain.
"""

import pytest
from conftest import IZAR_METER, SX1262, TIME

RADIO = """\
wmbus_radio:
  radio_type: {radio}
  cs_pin: GPIO8
  reset_pin: GPIO12
  irq_pin: GPIO14
  on_frame:
    - then:
        - lambda: 'ESP_LOGI("test", "%s %d", frame->as_rtlwmbus().c_str(), frame->rssi());'
"""


@pytest.mark.full
@pytest.mark.parametrize("radio", ["SX1262", "SX1276", "CC1101"])
def test_compile_radio(esphome, radio):
    r = esphome(RADIO.format(radio=radio) + TIME + IZAR_METER, mode="compile")
    assert r.ok, r.output[-4000:]


@pytest.mark.full
def test_compile_all_drivers(esphome):
    yaml = (
        SX1262
        + "  tcxo_voltage: 1.8V\n"
        + TIME
        + IZAR_METER
        + "wmbus_common:\n  drivers: all\n"
    )
    r = esphome(yaml, mode="compile")
    assert r.ok, r.output[-4000:]
