#include "../../../spin-neuron-impl.h"
#include "additive_one_term_impl.h"

//---------------------------------------
// Globals
//---------------------------------------
// Global plasticity parameter data
plasticity_weight_region_data_t plasticity_weight_region_data[SYNAPSE_TYPE_COUNT];

//---------------------------------------
// Functions
//---------------------------------------
address_t plasticity_region_weight_filled (address_t address, uint32_t flags)
{
  use(flags);

  log_info("plasticity_region_weight_filled: starting");
  log_info("\tSTDP additive one-term weight dependance");
  
  // Copy plasticity region data from address
  // **NOTE** this seems somewhat safer than relying on sizeof
  int32_t *plasticity_word = (int32_t*)address;
  for(uint32_t s = 0; s < SYNAPSE_TYPE_COUNT; s++)
  {
    plasticity_weight_region_data[s].min_weight = *plasticity_word++;
    plasticity_weight_region_data[s].max_weight = *plasticity_word++;
    plasticity_weight_region_data[s].a2_plus = *plasticity_word++;
    plasticity_weight_region_data[s].minus_a2_minus = -*plasticity_word++;
    
    log_info("\tSynapse type %u: Min weight:%d, Max weight:%d, A2+:%d, -A2-:%d", 
      s, plasticity_weight_region_data[s].min_weight, plasticity_weight_region_data[s].max_weight, 
        plasticity_weight_region_data[s].a2_plus, plasticity_weight_region_data[s].minus_a2_minus);
  }
  log_info("plasticity_region_weight_filled: completed successfully");

  // Return end address of region
  return (address_t)plasticity_word;
}
