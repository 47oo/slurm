#include "slurm_protocol_pack.h"
#include "pack.h"
#include <stdio.h>

extern int debug ;

void pack_header ( char ** buffer , uint32_t * length , header_t * header )
{
	pack16 ( header -> version , ( void ** ) buffer , length ) ;
	pack16 ( header -> flags , ( void ** ) buffer , length ) ;
	pack16 ( header -> message_type , ( void ** ) buffer , length ) ;
	pack32 ( header -> body_length , ( void ** ) buffer , length ) ;
}

void unpack_header ( char ** buffer , uint32_t * length , header_t * header )
{
	unpack16 ( & header -> version , ( void ** ) buffer , length ) ;
	unpack16 ( & header -> flags , ( void ** ) buffer , length ) ;
	unpack16 ( & header -> message_type , ( void ** ) buffer , length ) ;
	unpack32 ( & header -> body_length , ( void ** ) buffer , length ) ;
}

void pack_message ( char ** buffer , uint32_t * buf_len , slurm_message_t const * message )
{
	switch ( message -> message_type )
	{
		case REQUEST_NODE_REGISRATION_STATUS :
			break ;
		case MESSAGE_NODE_REGISRATION_STATUS :
			break ;
		case REQUEST_RESOURCE_ALLOCATION :
		case REQUEST_SUBMIT_BATCH_JOB :
			break ;
		case RESPONSE_RESOURCE_ALLOCATION :
			break ;
		case RESPONSE_SUBMIT_BATCH_JOB :
			break ;
		case REQUEST_CANCEL_JOB :
			break ;
		case REQUEST_CANCEL_JOB_STEP :
			break ;
		case REQUEST_SIGNAL_JOB :
			break ;
		case REQUEST_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_RECONFIGURE :
			break ;
		case RESPONSE_CANCEL_JOB :
		case RESPONSE_RECONFIGURE :
		case RESPONSE_CANCEL_JOB_STEP :
		case RESPONSE_SIGNAL_JOB :
		case RESPONSE_SIGNAL_JOB_STEP :
			break ;
		case REQUEST_JOB_INFO :
			break ;
		case REQUEST_JOB_ATTACH :
			break ;
		case RESPONSE_JOB_ATTACH :
			break ;
		case REQUEST_LAUNCH_TASKS :
			break ;
		case REQUEST_GET_JOB_STEP_INFO :
			break ;
		case RESPONSE_GET_JOB_STEP_INFO :
			break ;
		case REQUEST_JOB_RESOURCE :
			break ;
		case RESPONSE_JOB_RESOURCE :
			break ;
		case REQUEST_RUN_JOB_STEP :
			break ;
		case RESPONSE_RUN_JOB_STEP:
			break ;
		case REQUEST_GET_KEY :
			break ;
		case RESPONSE_GET_KEY :
			break ;
		case MESSAGE_TASK_EXIT :
			break ;
		case REQUEST_BATCH_JOB_LAUNCH :
			break ;
		case MESSAGE_UPLOAD_ACCOUNTING_INFO :
			break ;
		default :
			if ( debug )
			{
				fprintf ( stderr , "No pack method for message type %i",  message -> message_type ) ;
			}
			break;
		
	}
}

void unpack_message ( char ** buffer , uint32_t * buf_len , slurm_message_t * message )
{
	switch ( message -> message_type )
	{
	}
}

void pack_node_registration_status_message ( char ** buffer , uint32_t * length , node_registration_status_message_t * message )
{
	pack32 ( message -> timestamp , ( void ** ) buffer , length ) ;
	pack32 ( message -> memory_size , ( void ** ) buffer , length ) ;
	pack32 ( message -> temporary_disk_space , ( void ** ) buffer , length ) ;
}

void unpack_node_registration_status_message ( char ** buffer , uint32_t * length , node_registration_status_message_t * message )
{
	unpack32 ( & message -> timestamp , ( void ** ) buffer , length ) ;
	unpack32 ( & message -> memory_size , ( void ** ) buffer , length ) ;
	unpack32 ( & message -> temporary_disk_space , ( void ** ) buffer , length ) ;
}

/* template 
void pack_ ( char ** buffer , uint32_t * length , * message )
{
	pack16 ( message -> , buffer , length ) ;
	pack32 ( message -> , buffer , length ) ;
}

void unpack_ ( char ** buffer , uint32_t * length , * messge )
{
	unpack16 ( & message -> , buffer , length ) ;
	unpack32 ( & message -> , buffer , length ) ;
}
*/
