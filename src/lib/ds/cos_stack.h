/**
 * @file cos_stack.h
 * @brief Stack
 */

#ifndef COS_STACK_H
#define COS_STACK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ---------------------------------------------------*/
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/* Public macros ----------------------------------------------*/

/* Public typedefs --------------------------------------------*/
typedef struct cos_stack_t cos_stack_t;

typedef enum
{
    COS_STACK_CAPACITY_FIXED = 0,
    COS_STACK_CAPACITY_DYNAMIC,
} cos_stack_capacity_mode_t;
/* Public function prototypes --------------------------------*/

/**
 * @brief Create a stack
 * @param init_capacity Initial capacity, defaults to 4 when 0 is passed
 * @return cos_stack_t* Returns stack pointer if creation succeeds, returns NULL on failure
 */
cos_stack_t *cos_stack_create(size_t init_capacity);

/**
 * @brief Create a stack with specified capacity mode
 * @param init_capacity Initial capacity, defaults to 4 when 0 is passed
 * @param mode Capacity mode: fixed capacity or dynamic capacity
 * @return cos_stack_t* Returns stack pointer if creation succeeds, returns NULL on failure
 */
cos_stack_t *cos_stack_create_with_mode(size_t init_capacity, cos_stack_capacity_mode_t mode);

/**
 * @brief Push to stack
 * @param stack Stack pointer
 * @param data Data pointer
 * @return true Push successful
 * @return false Push failed
 */
bool cos_stack_push(cos_stack_t *stack, void *data);

/**
 * @brief Pop from stack
 * @param stack Stack pointer
 * @return void* Top element pointer, returns NULL if stack is empty or parameter is invalid
 */
void *cos_stack_pop(cos_stack_t *stack);

/**
 * @brief Get top element without popping
 * @param stack Stack pointer
 * @return void* Top element pointer, returns NULL if stack is empty or parameter is invalid
 */
void *cos_stack_peek(cos_stack_t *stack);

/**
 * @brief Get current number of elements in stack
 * @param stack Stack pointer
 * @return size_t Number of elements, returns 0 if parameter is invalid
 */
size_t cos_stack_get_size(cos_stack_t *stack);

/**
 * @brief Get stack capacity mode
 * @param stack Stack pointer
 * @return cos_stack_capacity_mode_t Stack capacity mode, returns fixed mode if parameter is invalid
 */
cos_stack_capacity_mode_t cos_stack_get_capacity_mode(cos_stack_t *stack);

/**
 * @brief Destroy stack and free memory
 * @param stack Stack pointer
 */
void cos_stack_destroy(cos_stack_t *stack);

/**
 * @brief Get element at specified index (0 = bottom, size-1 = top)
 * @param stack Stack pointer
 * @param index Element index (0-based from bottom)
 * @return void* Element pointer at index, returns NULL if index out of bounds or parameter invalid
 */
void *cos_stack_get_at(cos_stack_t *stack, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* COS_STACK_H */
