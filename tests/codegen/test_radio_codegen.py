"""wmbus_radio schema and code generation."""

import re

import pytest
from conftest import SX1262, TIME

# SX1261/2 datasheet, SetDIO3AsTCXOCtrl tcxoVoltage.
TCXO_VOLTAGES = {
    "1.6V": 0x00,
    "1.7V": 0x01,
    "1.8V": 0x02,
    "2.2V": 0x03,
    "2.4V": 0x04,
    "2.7V": 0x05,
    "3.0V": 0x06,
    "3.3V": 0x07,
}


def tcxo_arg(main_cpp):
    m = re.search(r"set_tcxo_voltage\(([^)]*)\)", main_cpp)
    assert m, "set_tcxo_voltage() not generated"
    return int(m.group(1), 0)


@pytest.mark.parametrize("voltage,code", TCXO_VOLTAGES.items())
def test_tcxo_voltage_reaches_generated_code(esphome, voltage, code):
    r = esphome(SX1262 + f"  tcxo_voltage: {voltage}\n" + TIME, generate=True)
    assert r.ok, r.output
    assert tcxo_arg(r.main_cpp) == code


def test_tcxo_voltage_default_is_3v0(esphome):
    r = esphome(SX1262 + TIME, generate=True)
    assert r.ok, r.output
    assert tcxo_arg(r.main_cpp) == 0x06
    assert "set_tcxo(true)" in r.main_cpp


def test_tcxo_voltage_lowercase_accepted(esphome):
    r = esphome(SX1262 + "  tcxo_voltage: 1.8v\n" + TIME)
    assert r.ok, r.output


@pytest.mark.parametrize("bad", ["1.9V", "1.8", "5V"])
def test_tcxo_voltage_invalid_rejected(esphome, bad):
    r = esphome(SX1262 + f"  tcxo_voltage: {bad}\n" + TIME)
    assert not r.ok
    assert "tcxo_voltage" in r.output


def test_tcxo_voltage_accepted_but_unused_on_cc1101(esphome):
    # The option is not radio-specific in the schema: a CC1101 config accepts
    # it and the generated call is simply ignored by the CC1101 driver.
    yaml = (
        "wmbus_radio:\n"
        "  radio_type: CC1101\n"
        "  cs_pin: GPIO8\n"
        "  irq_pin: GPIO14\n"
        "  tcxo_voltage: 1.8V\n" + TIME
    )
    r = esphome(yaml, generate=True)
    assert r.ok, r.output
    files = r.component_files("wmbus_radio")
    assert "transceiver_cc1101.cpp" in files
    assert "transceiver_sx1262.cpp" not in files


@pytest.mark.parametrize("radio", ["SX1262", "SX1276", "CC1101"])
def test_only_selected_transceiver_is_built(esphome, radio):
    yaml = (
        "wmbus_radio:\n"
        f"  radio_type: {radio}\n"
        "  cs_pin: GPIO8\n"
        "  reset_pin: GPIO12\n"
        "  irq_pin: GPIO14\n" + TIME
    )
    r = esphome(yaml, generate=True)
    assert r.ok, r.output
    transceivers = [
        f for f in r.component_files("wmbus_radio") if f.startswith("transceiver")
    ]
    assert sorted(transceivers) == sorted(
        ["transceiver.cpp", "transceiver.h"]
        + [f"transceiver_{radio.lower()}.cpp", f"transceiver_{radio.lower()}.h"]
    )


def test_unknown_radio_type_rejected(esphome):
    r = esphome(SX1262.replace("SX1262", "SX1280") + TIME)
    assert not r.ok


def test_sx1262_options_generated(esphome):
    yaml = SX1262 + "  rx_gain: POWER_SAVING\n  sync_mode: ULTRA_LOW_LATENCY\n" + TIME
    r = esphome(yaml, generate=True)
    assert r.ok, r.output
    assert 'set_rx_gain_mode("RX_GAIN_POWER_SAVING")' in r.main_cpp
    assert 'set_sync_mode("SYNC_MODE_ULTRA_LOW_LATENCY")' in r.main_cpp
    assert "set_rf_switch(true)" in r.main_cpp
