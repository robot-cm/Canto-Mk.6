/**
 * @file cos_sim_hw_mock.h
 * @brief Simulator hardware mock — registers fake device OPS so the services
 *        that depend on real hardware (time / battery / power / sensors) no
 *        longer fail with "device OPS not available" on the PC build.
 *
 * Compiled ONLY when COS_SIMULATOR is defined (i.e. the desktop simulator).
 * The real ESP-IDF firmware provides its own board-level drivers and never
 * includes this file.
 */

#ifndef COS_SIM_HW_MOCK_H
#define COS_SIM_HW_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "cos_core.h"

/**
 * @brief Register all mocked hardware devices (time / battery / power / sensors).
 *        Must be called BEFORE the services that consume them are initialized
 *        (e.g. before cos_service_battery_init()).
 */
void cos_sim_hw_mock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* COS_SIM_HW_MOCK_H */
