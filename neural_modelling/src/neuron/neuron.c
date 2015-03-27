/* neuron.c
 *
 * Dave Lester, Yuri , Abigail...
 *
 *  CREATION DATE
 *    1 August, 2013
 *
 *  HISTORY
 * *  DETAILS
 *    Created on       : 1 August 2013
 *    Version          : $Revision: 1.1 $
 *    Last modified on : $Date: 2013/08/06 15:55:57 $
 *    Last modified by : $Author: dave $
 *    $Id: neuron.c,v 1.1 2013/08/06 15:55:57 dave Exp dave $
 *
 *    $Log$
 *
 *
 */

#include "spin-neuron-impl.h"

#include <string.h>

// **NOTE** synapse shaping implementation and neuron headers gets included by compiler

#define REAL accum
#define REAL_CONST(x)   x##k

//REAL  global_I_this_timestep;

uint32_t       h;        // number of micro-seconds per time-step

// Neuron modelling data
neuron_pointer_t neuron_array;

timer_t  time;

// upper part of spike packet identifier for this core.
uint32_t  key;
uint32_t  num_neurons;
uint32_t  num_params;
uint32_t  ring_buffer_to_input_shift_direction[SYNAPSE_TYPE_COUNT];  // Whether shift is left or right
uint32_t  ring_buffer_to_input_shift_bits[SYNAPSE_TYPE_COUNT];        // Amount to shift the ring buffer by to make it an input


static bool record_neuron_param(recording_channel_e channel, uint8_t parameter, neuron_pointer_t neuron)
{
  // Get neuron parameter value
  accum parameterValue = neuron_get_state(parameter, neuron );

  // Return the result of recording accum value
  return recording_record(channel, &parameterValue, sizeof(accum));
}

// The following is LIF...
void neuron (index_t n)
{
  neuron_pointer_t neuron = &neuron_array[n];

  // Get excitatory and inhibitory input from synapses
  // **NOTE** this may be in either conductance or current units
  accum exc_neuron_input = get_exc_neuron_input(n);
  accum inh_neuron_input = get_inh_neuron_input(n);

  // Get external bias from any source of intrinsic plasticity
  accum external_bias = plasticity_get_intrinsic_bias(n);

  bool spike = neuron_state_update( exc_neuron_input, inh_neuron_input, external_bias, neuron );

  // If we should be recording potential, record this neuron parameter **YUCK** magic number
  if(system_data_test_bit(e_system_data_record_neuron_potential))
  {
    record_neuron_param(e_recording_channel_neuron_potential, 1, neuron);
  }

  // If we should be recording gsyn
  if(system_data_test_bit(e_system_data_record_neuron_gsyn))
  {
	  accum temp_record_input = exc_neuron_input - inh_neuron_input;  // waiting for neuron record API
    // Record to correct recording channel
    // **NOTE** offset current is not currently exposed by neuron model
    recording_record(e_recording_channel_neuron_gsyn, &temp_record_input, sizeof(accum));
  }

  if( spike )
  {
    plasticity_process_post_synaptic_event(n);
    out_spike(n);
  }
}

void constant_vector (address_t a, index_t n, uint32_t value)
{
  index_t i;

  for (i = 0; i < n; i++) a[i] = value;
}

bool neural_data_filled (address_t address, uint32_t flags)
{
  use(flags);

  log_info("neural_data_filled: starting");


  // changed from above for new file format 13-1-2014
  key   = address[0];
  log_info("\tkey = %08x, (x: %u, y: %u) proc: %u",
    key, key_x (key), key_y (key), key_p (key));

  num_neurons = address [1];
  num_params  = address [2];
  h           = address [3]; // number of micro seconds per time step.

   log_info("\tneurons = %u, params = %u, time step = %u",
     num_neurons, num_params, h);

  // Read ring buffer input shift for each synapse type
  for(index_t i = 0; i < SYNAPSE_TYPE_COUNT; i++)
  {
    ring_buffer_to_input_shift_direction[i] = address[4 + i];
    ring_buffer_to_input_shift_bits[i] = address[4 + SYNAPSE_TYPE_COUNT + i];
    log_info("\tsynapse type:%u - ring buffer to input shift direction:%u, bits:%u", i, ring_buffer_to_input_shift_direction[i], ring_buffer_to_input_shift_bits[i]);
  }

  // Allocate DTCM for new format neuron array and copy block of data
  neuron_array = (neuron_t*)spin1_malloc( num_neurons * sizeof(neuron_t) );
  if(neuron_array == NULL)
  {
    sentinel("Unable to allocate neuron array - Out of DTCM");
  }

  memcpy( neuron_array, &address[4 + (SYNAPSE_TYPE_COUNT * 2)], num_neurons * sizeof(neuron_t) );
  initialize_out_spikes (num_neurons);
  //print_neurons();
/*
  a  =  configuration_reader_offset(address, 17);
  if (!(vector_copied((uint32_t*)p21, num_neurons, a, flags))) {
    log_info("vector copy failed");
    return (false);
  }
*/
  /*
  // values from nest: iaf_psc_exp.cpp...

  use(a);
  hack_vector ((uint32_t*)v_membrane, num_neurons,  991146299); // -70mV
  hack_vector ((uint32_t*)v_threshold,num_neurons, 1486719449); // -55mV
  hack_vector ((uint32_t*)v_rest,     num_neurons,  991146299); // -70mV
  hack_vector ((uint32_t*)v_reset,    num_neurons,  991146299); // -70mV

  // h = 0.1ms
  // tau_ex = 2.0
  // tau_in = 2.0
  // p11_ex = exp (-h/tau_ex);
  // p11_in = exp (-h/tau_in); which equals above
  hack_vector ((uint32_t*)p11,     num_neurons,  2605029347); // e(-0.5)  ~ 0.606..

  // tau = 10ms
  // p22 = exp (-h/tau);
  hack_vector ((uint32_t*)p22,     num_neurons,  4252231657);
                                                    // e(-0.01) ~ 0.990..

  // C = 250pF
  // p21 = tau/(C*(1 -tau/tau_ex)) * p11_ex * (1 - exp (h*(1/tau_ex - 1/tau)))
  hack_vector ((uint32_t*)p21,     num_neurons,  42955); // ~ 0.0000100...

  // p20 = tau/(C*(1 -p22))
  hack_vector ((uint32_t*)p20,     num_neurons,  263457); // ~ 4.0200..
  */

  log_info("neural_data_filled: completed successfully");
  return (true);
}

/*
**NOTE** JK - commented out to prevent use as this data is no longer being read from neuron data
accum voltage_to_accum (voltage_t v)
{ return (decay_s1615 (v_max - v_min, v) + v_min); }*/

#ifdef DEBUG

void print_neurons (void)
{
  //bool    empty = true;
  index_t n;

  //for (n = 1; n < 2/*MAX_NEURON_SIZE*/; n ++)
  //  empty = empty && (v_membrane [n] == v_rest [n]);

  printf ("Neurons\n");

  //if (!empty) {
    printf ("-------------------------------------\n");
    for (n = 0; n < 1/*MAX_NEURON_SIZE*/; n++)
      neuron_print (&(neuron_array[n]));
    printf ("-------------------------------------\n");
    //}
}

#endif /*DEBUG*/
