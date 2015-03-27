#ifndef ADDITIVE_TWO_TERM_IMPL_H
#define ADDITIVE_TWO_TERM_IMPL_H

// Standard includes
#include <stdbool.h>
#include <stdint.h>

// Include debug header for log_info etc
#include "../../../../common/common-impl.h"

// Include generic plasticity maths functions
#include "../../common/maths.h"
#include "../../common/runtime_log.h"

//---------------------------------------
// Structures
//---------------------------------------
typedef struct
{
  int32_t min_weight;
  int32_t max_weight;
  
  int32_t a2_plus;
  int32_t minus_a2_minus;
  int32_t a3_plus;
  int32_t minus_a3_minus;
} plasticity_weight_region_data_t;

typedef struct weight_state_t
{
  int32_t initial_weight;
  
  int32_t a2_plus;
  int32_t a2_minus;
  int32_t a3_plus;
  int32_t a3_minus;
  
  const plasticity_weight_region_data_t *weight_region;
} weight_state_t;

//---------------------------------------
// Externals
//---------------------------------------
extern plasticity_weight_region_data_t plasticity_weight_region_data[SYNAPSE_TYPE_COUNT];

//---------------------------------------
// STDP weight dependance functions
//---------------------------------------
static inline weight_state_t weight_init(weight_t weight, index_t synapse_type)
{
  use(weight);

  return (weight_state_t){ 
    .initial_weight = (int32_t)weight, 
    .a2_plus = 0, .a2_minus = 0, 
    .a3_plus = 0, .a3_minus = 0, 
    .weight_region = &plasticity_weight_region_data[synapse_type] };
}
//---------------------------------------
static inline weight_state_t weight_apply_depression(weight_state_t state, int32_t a2_minus, int32_t a3_minus)
{
  state.a2_minus += a2_minus;
  state.a3_minus += a3_minus;
  return state;
}
//---------------------------------------
static inline weight_state_t weight_apply_potentiation(weight_state_t state, int32_t a2_plus, int32_t a3_plus)
{
  state.a2_plus += a2_plus;
  state.a3_plus += a3_plus;
  return state;
}
//---------------------------------------
static inline weight_t weight_get_final(weight_state_t new_state)
{
  // Scale potentiation and depression
  // **NOTE** A2+, A2-, A3+ and A3- are pre-scaled into weight format
  int32_t delta_weight = __smulbb(new_state.a2_plus, new_state.weight_region->a2_plus);
  delta_weight = __smlabb(new_state.a3_plus, new_state.weight_region->a3_plus, delta_weight);
  delta_weight = __smlabb(new_state.a2_minus, new_state.weight_region->minus_a2_minus, delta_weight);
  delta_weight = __smlabb(new_state.a3_minus, new_state.weight_region->minus_a3_minus, delta_weight);

  // Apply all terms to initial weight
  int32_t new_weight = new_state.initial_weight + (delta_weight >> STDP_FIXED_POINT);

  // Clamp new weight
  new_weight = MIN(new_state.weight_region->max_weight, MAX(new_weight, new_state.weight_region->min_weight));
  
  plastic_runtime_log_info("\told_weight:%u, a2+:%d, a2-:%d, a3+:%d, a3-:%d",
    new_state.initial_weight, new_state.a2_plus, new_state.a2_minus, new_state.a3_plus, new_state.a3_minus);
  plastic_runtime_log_info("\tscaled a2+:%d, scaled a2-:%d, scaled a3+:%d, scaled a3-:%d, new_weight:%d",
    scaled_a2_plus, scaled_a2_minus, scaled_a3_plus, scaled_a3_minus, new_weight); 
  
  return (weight_t)new_weight;
}
#endif  // ADDITIVE_TWO_TERM_IMPL_H