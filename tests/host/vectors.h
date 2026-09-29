// Telegrams shared by several tests. Taken from the wmbusmeters test vectors
// in components/wmbus_common/driver_izar.cpp (without the DLL CRCs).
#pragma once

namespace test_vectors {
// IzarWater izar 21242472 NOKEY: total_m3 3.488, last_month_total_m3 3.486,
// current_alarms "meter_blocked,underflow", remaining_battery_life_y 14.5
inline constexpr const char *IZAR_1 =
    "1944304C72242421D401A2013D4013DD8B46A4999C1293E582CC";
inline constexpr const char *IZAR_1_ID = "21242472";

// IzarWater2 izar 66236629 NOKEY: total_m3 16.76
inline constexpr const char *IZAR_2 =
    "2944A511780729662366A20118001378D3B3DB8CEDD77731F25832AAF3DA8CADF9774EA6"
    "73172E8C61F2";
inline constexpr const char *IZAR_2_ID = "66236629";
} // namespace test_vectors
