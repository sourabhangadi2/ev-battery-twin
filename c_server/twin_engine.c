/*
 * twin_engine.c — Digital Twin math. Ports app/twin_engine.py verbatim to C.
 */
#include "twin_engine.h"
#include "database.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

/* Battery specification constants (same as Python) */
#define V_MIN                 3.0
#define V_MAX                 4.2
#define NOMINAL_CAPACITY_AH   2.5
#define R_INTERNAL_BASE       0.15

/* Simple LCG for occasional random increment (mirrors Python random.random() < 0.05) */
static unsigned int _rand_state = 42;
static double _lcg_rand(void) {
    _rand_state = _rand_state * 1664525u + 1013904223u;
    return (_rand_state & 0x7FFFFFFF) / (double)0x7FFFFFFF;
}

int update_digital_twin(SensorData *sensor, BatteryHealth *health_out) {
    /* 1. SOC via voltage (clamped 0..100) */
    double voltage = sensor->voltage;
    double soc = ((voltage - V_MIN) / (V_MAX - V_MIN)) * 100.0;
    if (soc < 0.0)   soc = 0.0;
    if (soc > 100.0) soc = 100.0;
    sensor->battery_percentage = soc;
    db_sensor_update_soc(sensor->id, soc);

    /* 2. Cycle count */
    BatteryHealth prev_health;
    int prev_cycles = 0;
    if (db_health_latest(&prev_health)) {
        prev_cycles = prev_health.estimated_cycles;
    }

    /* Time delta from previous sensor row */
    double time_delta_sec = 1.0;
    SensorData prev_sensor;
    if (db_sensor_prev(sensor->id, &prev_sensor)) {
        double dt = difftime(sensor->timestamp, prev_sensor.timestamp);
        if (dt > 0.0 && dt < 60.0) time_delta_sec = dt;
    }

    /* Coulomb counting: cycles = sum(|I|*dt) / (2 * Capacity_Ah * 3600) */
    double cur_mag = fabs(sensor->current);
    double cycle_inc = (cur_mag * time_delta_sec) / (2.0 * NOMINAL_CAPACITY_AH * 3600.0);
    if (cycle_inc > 0.05) cycle_inc = 0.0001;

    /* Accumulate cycles as float, store as int */
    double acc_cycles = (double)prev_cycles + cycle_inc * 1000.0;
    int est_cycles = (int)acc_cycles;
    if (est_cycles == prev_cycles && cycle_inc > 0) {
        if (_lcg_rand() < 0.05) est_cycles++;
    }

    /* 3. SOH & Degradation */
    double temp = sensor->temperature;
    double thermal_penalty = 0.02 * (temp > 25.0 ? temp - 25.0 : 0.0);
    double degradation = (0.005 * est_cycles) + thermal_penalty;
    if (degradation < 0.0)  degradation = 0.0;
    if (degradation > 30.0) degradation = 30.0;

    double soh = 100.0 - degradation;
    if (soh < 0.0)   soh = 0.0;
    if (soh > 100.0) soh = 100.0;

    /* 4. RUL */
    int rul = 0;
    double rul_f = 1000.0 * ((soh - 70.0) / 30.0);
    if (rul_f > 0) rul = (int)rul_f;

    /* Fill health record */
    health_out->health_score             = (double)((int)(soh * 100.0 + 0.5)) / 100.0;
    health_out->degradation              = (double)((int)(degradation * 100.0 + 0.5)) / 100.0;
    health_out->estimated_cycles         = est_cycles;
    health_out->estimated_remaining_life = rul;
    health_out->timestamp                = sensor->timestamp;

    db_health_insert(health_out);
    return 1;
}

double get_efficiency(double voltage, double current, double temp) {
    if (fabs(current) < 0.01) return 100.0;

    double r_int = R_INTERNAL_BASE + (0.002 * (temp > 25.0 ? temp - 25.0 : 0.0));
    double p_loss = current * current * r_int;
    double p_total = voltage * fabs(current);
    if (p_total <= 0.0) return 100.0;

    double eff = (1.0 - (p_loss / p_total)) * 100.0;
    if (eff < 0.0)   eff = 0.0;
    if (eff > 100.0) eff = 100.0;
    return eff;
}
