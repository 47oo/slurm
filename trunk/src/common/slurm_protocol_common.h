#ifndef _SLURM_PROTOCOL_COMMON_H
#define _SLURM_PROTOCOL_COMMON_H

#if HAVE_CONFIG_H
#  include <config.h>
#  if HAVE_INTTYPES_H
#    include <inttypes.h>
#  else
#    if HAVE_STDINT_H
#      include <stdint.h>
#    endif
#  endif  /* HAVE_INTTYPES_H */
#else   /* !HAVE_CONFIG_H */
#  include <inttypes.h>
#endif  /*  HAVE_CONFIG_H */

#include <netinet/in.h>

#define AF_SLURM AF_INET
#define SLURM_INADDR_ANY 0x00000000

/* LINUX SPECIFIC */
/* this is the slurm equivalent of the operating system file descriptor, which in linux is just an int */
typedef uint32_t slurm_fd ;

/* this is the slurm equivalent of the BSD sockets sockaddr */
typedef struct sockaddr_in slurm_addr ; 
/*struct kevin {
	int16_t family ;
	uint16_t port ;
	uint32_t address ;
	char pad[16 - sizeof ( int16_t ) - sizeof (uint16_t) - sizeof (uint32_t) ] ;
} ;
*/

/* SLURM datatypes */
/* this is a custom data type to describe the slurm msg type type that is placed in the slurm protocol header
 * while just an short now, it may change in the future */
/* Now defined in ../../src/common/slurm_protocol_defs.h
 * typedef uint16_t slurm_msg_type_t ;
 */

#endif
