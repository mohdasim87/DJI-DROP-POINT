// core/atmosphere.h
//
// Barometric pressure and air-density model. See spec section 6.1.
#pragma once

#include "solution.h"

namespace droppoint::core::atmosphere {

// Pressure at a given height above sea level, using the ICAO standard
// atmosphere approximation valid in the troposphere:
//   p = 101325 * (1 - 2.25577e-5 * h_asl)^5.25588
double PressurePa(double h_asl_m);

// Air density from pressure and temperature (ideal gas law with the dry-air
// specific gas constant R = 287.05 J/(kg*K)):
//   rho = p / (R * (T_celsius + 273.15))
double DensityKgM3(double pressure_pa, double temperature_c);

// Convenience: density directly from altitude and temperature.
AtmosphereState AtAltitude(double h_asl_m, double temperature_c);

}  // namespace droppoint::core::atmosphere
