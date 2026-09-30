#ifndef EOS_PROFMONITOR_H
#define EOS_PROFMONITOR_H

#include "eos_activity.h"

#if defined(CONFIG_PROFMONITOR_APP_ENABLE) && CONFIG_PROFMONITOR_APP_ENABLE

void eos_profmonitor_enter(void);

#endif /* CONFIG_PROFMONITOR_APP_ENABLE */

#endif /* EOS_PROFMONITOR_H */
