#ifndef _SLURM_PROTOCOL_UTIL_H

#include "slurm_protocol_defs.h"
#include "slurm_protocol_common.h"
#include <stdint.h>
uint32_t check_header_version( header_t * header) ;
void init_header ( header_t * header , slurm_message_type_t message_type , uint16_t flags ) ;
#endif
