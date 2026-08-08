#include "atmosphere.h"

#include <cmath>

namespace droppoint::core::atmosphere {

namespace {
constexpr double kSeaLevelPressurePa = 101325.0;
constexpr double kLapseCoefficient = 2.25577e-5;  // 1/m
constexpr double kLapseExponent = 5.25588;
constexpr double kSpecificGasConstantDryAir = 287.05;  // J/(kg*K)
constexpr double kCelsiusToKelvin = 273.15;
}  // namespace

double PressurePa(double h_asl_m) {
    return kSeaLevelPressurePa * std::pow(1.0 - kLapseCoefficient * h_asl_m, kLapseExponent);
}

double DensityKgM3(double pressure_pa, double temperature_c) {
    return pressure_pa / (kSpecificGasConstantDryAir * (temperature_c + kCelsiusToKelvin));
}

AtmosphereState AtAltitude(double h_asl_m, double temperature_c) {
    AtmosphereState s;
    s.pressure_pa = PressurePa(h_asl_m);
    s.temperature_c = temperature_c;
    s.density_kg_m3 = DensityKgM3(s.pressure_pa, temperature_c);
    return s;
}

}  // namespace droppoint::core::atmosphere
