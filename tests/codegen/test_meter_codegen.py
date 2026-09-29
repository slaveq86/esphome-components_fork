"""wmbus_meter / wmbus_common schema and code generation."""

import pytest
from conftest import IZAR_METER, SX1262, TIME


def drivers(result):
    return [
        f for f in result.component_files("wmbus_common") if f.startswith("driver_")
    ]


def test_izar_meter_generated(esphome):
    r = esphome(SX1262 + TIME + IZAR_METER, generate=True)
    assert r.ok, r.output
    # meter_id is passed as 8 lowercase hex digits.
    assert 'set_meter_params("2124589c", "izar", ""' in r.main_cpp
    assert "extern bool wmbus_driver_izar_linked;" in r.main_cpp


def test_only_selected_driver_is_built(esphome):
    r = esphome(SX1262 + TIME + IZAR_METER, generate=True)
    assert r.ok, r.output
    assert drivers(r) == ["driver_izar.cpp"]


def test_drivers_all(esphome):
    yaml = SX1262 + TIME + IZAR_METER + "wmbus_common:\n  drivers: all\n"
    r = esphome(yaml, generate=True)
    assert r.ok, r.output
    assert len(drivers(r)) > 80


def test_driver_alias_accepted(esphome):
    # driver_kamheat.cpp also registers "multical603".
    r = esphome(SX1262 + TIME + IZAR_METER.replace("izar", "multical603"))
    assert r.ok, r.output


def test_unknown_driver_rejected(esphome):
    r = esphome(SX1262 + TIME + IZAR_METER.replace("izar", "no_such_meter"))
    assert not r.ok


def test_key_hex_and_ascii(esphome):
    hex_key = IZAR_METER.replace(
        "    mode: [T1]\n", "    mode: [T1]\n    key: 00112233445566778899AABBCCDDEEFF\n"
    )
    r = esphome(SX1262 + TIME + hex_key, generate=True)
    assert r.ok, r.output
    bad_key = IZAR_METER.replace("    mode: [T1]\n", "    mode: [T1]\n    key: 0011\n")
    assert not esphome(SX1262 + TIME + bad_key).ok


@pytest.mark.xfail(
    strict=True,
    reason="upstream bug: wmbus_meter.h includes time/real_time_clock.h but "
    "the component neither depends on nor auto-loads `time`",
)
def test_meter_without_time_component(esphome):
    # Without `time:` the config validates, but the build then fails with
    # "real_time_clock.h: No such file or directory". Correct behaviour would be
    # to reject the config or load `time` automatically.
    r = esphome(SX1262 + IZAR_METER, generate=True)
    if not r.ok:
        return  # rejected with a config error: fine
    time_header = r.build / "src" / "esphome" / "components" / "time"
    assert time_header.is_dir(), "time component not included in the build"
