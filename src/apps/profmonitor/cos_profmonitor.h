#ifndef COS_PROFMONITOR_H
#define COS_PROFMONITOR_H

#include "cos_activity.h"

#if defined(CONFIG_PROFMONITOR_APP_ENABLE) && CONFIG_PROFMONITOR_APP_ENABLE

void cos_profmonitor_enter(void);

#endif /* CONFIG_PROFMONITOR_APP_ENABLE */

#endif /* COS_PROFMONITOR_H */
