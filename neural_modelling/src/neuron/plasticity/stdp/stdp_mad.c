// Spinn_common includes
#include "static-assert.h"

// sPyNNaker neural modelling includes
#include "../../spin-neuron-impl.h"
#include "../../synapses_impl.h"

// Plasticity common includes
#include "../common/maths.h"
#include "../common/runtime_log.h"
#include "../common/post_events_impl.h"
#include <string.h>

#ifdef DEBUG
  bool plastic_runtime_log_enabled = false;
#endif  // DEBUG

#ifdef SYNAPSE_BENCHMARK
  extern uint32_t num_plastic_pre_synaptic_events;
#endif  // SYNAPSE_BENCHMARK

//---------------------------------------
// Macros
//---------------------------------------
// The plastic control words used by Morrison synapses store an axonal delay in the upper 3 bits
// Assuming a maximum of 16 delay slots, this is all that is required as:
//
// 1) Dendritic + Axonal <= 15
// 2) Dendritic >= Axonal
// 
// Therefore:
//
// * Maximum value of dendritic delay is 15 (with axonal delay of 0) - It requires 4 bits
// * Maximum value of axonal delay is 7 (with dendritic delay of 8) - It requires 3 bits
//
//             |        Axonal delay       |  Dendritic delay   |       Type        |      Index         |
//             |---------------------------|--------------------|-------------------|--------------------|
// Bit count   | SYNAPSE_AXONAL_DELAY_BITS | SYNAPSE_DELAY_BITS | SYNAPSE_TYPE_BITS | SYNAPSE_INDEX_BITS |
//             |                           |                    |        SYNAPSE_TYPE_INDEX_BITS         |
//             |---------------------------|--------------------|----------------------------------------|
#ifndef SYNAPSE_AXONAL_DELAY_BITS
  #define SYNAPSE_AXONAL_DELAY_BITS 3
#endif

#define SYNAPSE_AXONAL_DELAY_MASK       ((1 << SYNAPSE_AXONAL_DELAY_BITS) - 1)

#define SYNAPSE_DELAY_TYPE_INDEX_BITS   (SYNAPSE_DELAY_BITS + SYNAPSE_TYPE_INDEX_BITS)

#if (SYNAPSE_DELAY_TYPE_INDEX_BITS + SYNAPSE_AXONAL_DELAY_BITS) > 16
  #error "Not enough bits for axonal synaptic delay bits"
#endif

//---------------------------------------
// Structures
//---------------------------------------
typedef struct
{
  pre_trace_t prev_trace;
  uint32_t prev_time;
} pre_event_history_t;

//---------------------------------------
// Synapse update loop
//---------------------------------------
static inline final_state_t plasticity_update_synapse(const uint32_t last_pre_time, const pre_trace_t last_pre_trace,
  const pre_trace_t new_pre_trace, const uint32_t delay_dendritic, const uint32_t delay_axonal, 
  update_state_t current_state, const post_event_history_t *post_event_history)
{
  // Apply axonal delay to time of last presynaptic spike
  const uint32_t delayed_last_pre_time = last_pre_time + delay_axonal;
  
  // Get the post-synaptic window of events to be processed
  const uint32_t window_begin_time = delayed_last_pre_time - delay_dendritic;
  const uint32_t window_end_time = time + delay_axonal - delay_dendritic;
  post_event_window_t post_window = post_get_window_delayed(post_event_history, window_begin_time, window_end_time);

  plastic_runtime_log_info("\tPerforming deferred synapse update at time:%u", time);
  plastic_runtime_log_info("\t\tbegin_time:%u, end_time:%u - prev_time:%u, num_events:%u",
    window_begin_time, window_end_time, post_window.prev_time, post_window.num_events);

  // Process events in post-synaptic window
  while(post_window.num_events > 0)
  {
    const uint32_t delayed_post_time = *post_window.next_time + delay_dendritic;
    plastic_runtime_log_info("\t\tApplying post-synaptic event at delayed time:%u\n", delayed_post_time);
    
    // Apply spike to state
    current_state = timing_apply_post_spike(delayed_post_time, *post_window.next_trace, 
      delayed_last_pre_time, last_pre_trace, 
      post_window.prev_time, post_window.prev_trace, 
      current_state);
    
    // Go onto next event
    post_window = post_next_delayed(post_window, delayed_post_time);
  }
  
  const uint32_t delayed_pre_time = time + delay_axonal;
  plastic_runtime_log_info("\t\tApplying pre-synaptic event at time:%u last post time:%u\n", delayed_pre_time, post_window.prev_time);
      
  // Apply spike to state 
  // **NOTE** dendritic delay is subtracted
  current_state = timing_apply_pre_spike(delayed_pre_time, new_pre_trace, 
    delayed_last_pre_time, last_pre_trace, 
    post_window.prev_time, post_window.prev_trace, 
    current_state);

  // Return final synaptic word and weight
  return synapse_get_final(current_state);
}

//---------------------------------------
// PACMAN memory region reading
//---------------------------------------
void initialise_plasticity_buffers()
{
  log_info("initialise_plasticity_buffers: starting");
  
  post_init_buffers();
  
  log_info("initialise_plasticity_buffers: completed successfully");
}

//---------------------------------------
// Synaptic row plastic-region implementation
//---------------------------------------
static inline plastic_synapse_t* plastic_synapses(address_t plastic)
{
  const uint32_t pre_event_history_size_words = sizeof(pre_event_history_t) / sizeof(uint32_t);
 static_assert(pre_event_history_size_words * sizeof(uint32_t) == sizeof(pre_event_history_t), "Size of pre_event_history_t structure should be a multiple of 32-bit words");

  return (plastic_synapse_t*)(&plastic[pre_event_history_size_words]); 
}
//---------------------------------------
static inline pre_event_history_t *plastic_event_history(address_t plastic)
{
  return (pre_event_history_t*)(&plastic[0]);
}
//---------------------------------------
static inline index_t sparse_axonal_delay(uint32_t x)
{ 
  return ((x >> SYNAPSE_DELAY_TYPE_INDEX_BITS) & SYNAPSE_AXONAL_DELAY_MASK); 
}
//---------------------------------------
void plasticity_process_post_synaptic_event(uint32_t j)
{
#ifdef DEBUG
  plastic_runtime_log_enabled = true;
#endif  // DEBUG

  plastic_runtime_log_info("Adding post-synaptic event to trace at time:%u\n", time);
  
  // Add post-event
  post_event_history_t *history = &post_event_history[j];
  const uint32_t last_post_time = history->times[history->count_minus_one];
  const post_trace_t last_post_trace = history->traces[history->count_minus_one];
  post_add(history, timing_add_post_spike(last_post_time, last_post_trace));
}
//---------------------------------------
accum plasticity_get_intrinsic_bias(uint32_t j)
{
  use(j);
  
  return 0.0k;
}
//---------------------------------------
void process_plastic_synapses (address_t plastic, address_t fixed, ring_entry_t *ring_buffer)
{
#ifdef DEBUG
  plastic_runtime_log_enabled = true;
#endif  // DEBUG

  // Extract seperate arrays of plastic synapses (from plastic region),
  // Control words (from fixed region) and number of plastic synapses
  plastic_synapse_t *plastic_words = plastic_synapses(plastic);
  const control_t *control_words = plastic_controls(fixed);
  size_t plastic_synapse  = num_plastic_controls(fixed);

#ifdef SYNAPSE_BENCHMARK
  num_plastic_pre_synaptic_events += plastic_synapse;
#endif  // SYNAPSE_BENCHMARK
  
  // Get event history from synaptic row
  pre_event_history_t *event_history = plastic_event_history(plastic);

  // Get last pre-synaptic event from event history
  const uint32_t last_pre_time = event_history->prev_time;
  const pre_trace_t last_pre_trace = event_history->prev_trace;

  // Update pre-synaptic trace
  plastic_runtime_log_info("Adding pre-synaptic event to trace at time:%u", time);
  event_history->prev_time = time;
  event_history->prev_trace = timing_add_pre_spike(last_pre_time, last_pre_trace);
  
  // Loop through plastic synapses
  for ( ; plastic_synapse > 0; plastic_synapse--) 
  {
    // Get next control word (autoincrementing)
    uint32_t control_word = *control_words++;

     // Extract control-word components
    // **NOTE** cunningly, control word is just the same as lower 
    // 16-bits of 32-bit fixed synapse so same functions can be used
    uint32_t delay_axonal = 0;//sparse_axonal_delay(control_word);
    uint32_t delay_dendritic = sparse_delay(control_word);
    uint32_t type = sparse_type(control_word);
    uint32_t index = sparse_index(control_word);
    uint32_t type_index = sparse_type_index(control_word);
    
    // Create update state from the plastic synaptic word
    update_state_t current_state = synapse_init(*plastic_words, type);
    
    // Update the synapse state
    final_state_t final_state = plasticity_update_synapse(last_pre_time, last_pre_trace, 
      event_history->prev_trace, delay_dendritic, delay_axonal,
      current_state, &post_event_history[index]);
    
    // Convert into ring buffer offset
    uint32_t offset = offset_sparse(delay_axonal + delay_dendritic + time, type_index);

    // Add weight to ring-buffer entry
    // **NOTE** Dave suspects that this could be a potential location for overflow
    ring_buffer[offset] += synapse_get_final_weight(final_state);

    // Write back updated synaptic word to plastic region
    *plastic_words++ = synapse_get_final_synaptic_word(final_state);
  }
}
//---------------------------------------
bool plasticity_region_filled (uint32_t *address, uint32_t flags)
{
  use(flags);
  
  // Load timing dependence data
  address = plasticity_region_trace_filled(address, flags);
  
  // Load weight dependence data
  plasticity_region_weight_filled(address, flags);
  
  return true;
}
//---------------------------------------
#ifdef DEBUG
void print_plastic_synapses(address_t plastic, address_t fixed)
{
  // Extract seperate arrays of weights (from plastic region),
  // Control words (from fixed region) and number of plastic synapses
  weight_t *plastic_words = plastic_synapses(plastic);
  const control_t *control_words = plastic_controls(fixed);
  size_t plastic_synapse  = num_plastic_controls(fixed);
  const pre_event_history_t *event_history = plastic_event_history(plastic);

  printf ("Plastic region %u synapses\n", plastic_synapse);

  // Loop through plastic synapses
  for (uint32_t i = 0; i < plastic_synapse; i++) 
  {
    // Get next weight and control word (autoincrementing control word)
    uint32_t weight = *plastic_words++;
    uint32_t control_word = *control_words++;

    printf ("%08x [%3d: (w: %5u (=", control_word, i, weight);
    print_weight (sparse_type(control_word), weight);
    printf ("nA) d: %2u, %s, n = %3u)] - {%08x %08x}\n",
      sparse_delay(control_word),
      get_synapse_type_char(sparse_type(control_word)),
      sparse_index(control_word),
      SYNAPSE_DELAY_MASK,
      SYNAPSE_TYPE_INDEX_BITS
    );
  }
}
#endif  // DEBUG