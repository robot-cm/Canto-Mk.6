/**
 * @file eos_sim_hw_mock.h
 * @brief Simulator hardware mock — registers fake device OPS so the services
 *        that depend on real hardware (time / battery / power / sensors) no
 *        longer fail with "device OPS not available" on the PC build.
 *
 * Compiled ONLY when EOS_SIMULATOR is defined (i.e. the desktop simulator).
 * The real ESP-IDF firmware provides its own board-level drivers and never
 * includes this file.
 */

#ifndef EOS_SIM_HW_MOCK_H
#define EOS_SIM_HW_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "eos_core.h"

/**
 * @brief Register all mocked hardware devices (time / battery / power / sensors).
 *        Must be called BEFORE the services that consume them are initialized
 *        (e.g. before eos_service_battery_init()).
 */
void eos_sim_hw_mock_init(void);

#ifdef __cplusplus
}
#endif

#endif /* EOS_SIM_HW_MOCK_H */
