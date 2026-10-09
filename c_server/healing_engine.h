/*
 * healing_engine.h — Self-Healing safety state machine.
 * Ports app/healing_engine.py verbatim to C.
 */
#ifndef HEALING_ENGINE_H
#define HEALING_ENGINE_H

#include "models.h"

/*
 * Evaluate safety limits and apply self-healing actuator adjustments.
 * Modifies cmd_out with updated relay/fan/buzzer states and saves to DB.
 * Returns 1 on success.
 */
int run_self_healing_engine(const SensorData *sensor, Command *cmd_out);

#endif /* HEALING_ENGINE_H */
