/*
 * twin_engine.h — Digital Twin estimation (SOC, SOH, RUL, efficiency).
 * Ports app/twin_engine.py verbatim to C.
 */
#ifndef TWIN_ENGINE_H
#define TWIN_ENGINE_H

#include "models.h"

/*
 * Compute Digital Twin parameters from latest sensor reading.
 * Updates sensor->battery_percentage (SOC).
 * Fills health_out with new BatteryHealth and inserts it into DB.
 * Returns 1 on success.
 */
int  update_digital_twin(SensorData *sensor, BatteryHealth *health_out);

/*
 * Compute instantaneous efficiency percentage.
 */
double get_efficiency(double voltage, double current, double temp);

#endif /* TWIN_ENGINE_H */
