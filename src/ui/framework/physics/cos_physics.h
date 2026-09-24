/**
 * @file eos_physics.h
 * @brief 1-D inertial scroller with friction, spring and bounce (req #4).
 *
 * Pure math, fully unit-testable headlessly. Drives the ArcList scroll offset
 * (in arc-degrees) and any other 1-D gesture-driven motion.
 */
#ifndef EOS_PHYSICS_H
#define EOS_PHYSICS_H

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
} eos_scroller_t;

/** @brief Initialize a scroller with bounds [min,max] and default params. */
void eos_scroller_init(eos_scroller_t *s, float min, float max);

/** @brief Override friction/spring tuning. */
void eos_scroller_set_params(eos_scroller_t *s, float decay,
                             float spring_k, float spring_c);

/** @brief Finger drags: shift pos by delta (deg) over dt seconds; tracks vel. */
void eos_scroller_drag(eos_scroller_t *s, float delta, float dt);

/** @brief Finger released: begin inertial + bounce motion. */
void eos_scroller_release(eos_scroller_t *s);

/** @brief Advance the simulation by dt seconds (one frame). */
void eos_scroller_step(eos_scroller_t *s, float dt);

/** @brief True when settled (not dragging, ~0 velocity, within bounds). */
bool eos_scroller_is_resting(const eos_scroller_t *s);

#ifdef __cplusplus
}
#endif
#endif /* EOS_PHYSICS_H */
