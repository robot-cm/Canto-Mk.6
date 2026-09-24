/**
 * @file cos_physics.h
 * @brief 1-D inertial scroller with friction, spring and bounce (req #4).
 *
 * Pure math, fully unit-testable headlessly. Drives the ArcList scroll offset
 * (in arc-degrees) and any other 1-D gesture-driven motion.
 */
#ifndef COS_PHYSICS_H
#define COS_PHYSICS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float pos;        /**< current position (arc-degrees)            */
    float vel;        /**< velocity (deg/sec)                       */
    float min;        /**< lower bound                             */
    float max;        /**< upper bound                             */
    float decay;      /**< velocity decay rate / sec (friction)    */
    float spring_k;   /**< spring stiffness for bounce             */
    float spring_c;   /**< spring damping                          */
    bool  dragging;   /**< true while finger is down               */
} cos_scroller_t;

/** @brief Initialize a scroller with bounds [min,max] and default params. */
void cos_scroller_init(cos_scroller_t *s, float min, float max);

/** @brief Override friction/spring tuning. */
void cos_scroller_set_params(cos_scroller_t *s, float decay,
                             float spring_k, float spring_c);

/** @brief Finger drags: shift pos by delta (deg) over dt seconds; tracks vel. */
void cos_scroller_drag(cos_scroller_t *s, float delta, float dt);

/** @brief Finger released: begin inertial + bounce motion. */
void cos_scroller_release(cos_scroller_t *s);

/** @brief Advance the simulation by dt seconds (one frame). */
void cos_scroller_step(cos_scroller_t *s, float dt);

/** @brief True when settled (not dragging, ~0 velocity, within bounds). */
bool cos_scroller_is_resting(const cos_scroller_t *s);

#ifdef __cplusplus
}
#endif
#endif /* COS_PHYSICS_H */
