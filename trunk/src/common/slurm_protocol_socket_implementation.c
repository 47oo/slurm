/*****************************************************************************\
 *  slurm_protocol_socket_implementation.c - slurm communications interfaces 
 *	based upon sockets
 *****************************************************************************
 *  Copyright (C) 2002 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Kevin Tew <tew1@llnl.gov>
 *  UCRL-CODE-2002-040.
 *  
 *  This file is part of SLURM, a resource management program.
 *  For details, see <http://www.llnl.gov/linux/slurm/>.
 *  
 *  SLURM is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *  
 *  SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
 *  WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 *  FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
 *  details.
 *  
 *  You should have received a copy of the GNU General Public License along
 *  with SLURM; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
\*****************************************************************************/
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <errno.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/types.h>
#include <signal.h>
#include <stdio.h>

#if HAVE_SYS_SOCKET_H
#  include <sys/socket.h>
#else
#  if HAVE_SOCKET_H
#    include <socket.h>
#  endif
#endif

#include <src/common/slurm_protocol_interface.h>
#include <src/common/slurm_protocol_common.h>
#include <src/common/slurm_protocol_defs.h>
#include <src/common/log.h>
#include <src/common/pack.h>

/*****************************************************************
 * MIDDLE LAYER MSG FUNCTIONS
 ****************************************************************/
slurm_fd _slurm_init_msg_engine ( slurm_addr * slurm_address )
{
	return _slurm_listen_stream ( slurm_address ) ;
}

slurm_fd _slurm_open_msg_conn ( slurm_addr * slurm_address )
{
	return _slurm_open_stream ( slurm_address ) ;
}

/* this should be a no-op that just returns open_fd in a message implementation */
slurm_fd _slurm_accept_msg_conn ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	return _slurm_accept_stream ( open_fd , slurm_address ) ;
}	
/* this should be a no-op in a message implementation */
int _slurm_close_accepted_conn ( slurm_fd open_fd ) 
{
	return _slurm_close ( open_fd ) ;
}

ssize_t _slurm_msg_recvfrom ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	size_t recv_len ;

	char size_buffer_temp [8] ;
	char * size_buffer = size_buffer_temp ;
	char * moving_buffer = NULL ;
	unsigned int size_buffer_len = 8 ;
	unsigned int transmit_size ;
	unsigned int total_len ;

	moving_buffer = size_buffer ;
	total_len = 0 ;
	while ( total_len < sizeof ( uint32_t ) )
	{	
		if ( ( recv_len = _slurm_recv ( open_fd , moving_buffer , sizeof ( uint32_t ) , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) ) == SLURM_SOCKET_ERROR  )
		{
			if ( errno ==  EINTR )
			{
				continue ;
			}
			else
			{
				return SLURM_PROTOCOL_ERROR ;
			}
		}
		else if ( recv_len > 0 )
		{
			total_len += recv_len ;
			moving_buffer += recv_len ;
		}
		else if ( recv_len == 0 )
		{
			/*debug ( "Error receiving length of datagram. recv_len = 0 ") ; */
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else 
		{
			/*debug ( "We don't handle negative return codes > -1") ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
	}
	unpack32 ( & transmit_size , ( void ** ) & size_buffer , & size_buffer_len ) ;

	moving_buffer = buffer ;
	total_len = 0 ;
	while ( total_len < transmit_size )
	{
		if ( ( recv_len = _slurm_recv ( open_fd , moving_buffer , transmit_size , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) ) == SLURM_SOCKET_ERROR )
		{
			if ( errno ==  EINTR )
			{
				continue ;
			}
			else
			{
				return SLURM_PROTOCOL_ERROR ;
			}
			return recv_len ;
		}
		else if ( recv_len > 0 )
		{
			total_len += recv_len ;
			moving_buffer += recv_len ;
		}
		else if ( recv_len == 0 )
		{
			/*debug ( "Error receiving length of datagram. recv_len = 0 ") ; */
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_ZERO_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else 
		{
			/*debug ( "We don't handle negative return codes > -1") ;*/
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NEGATIVE_RECV_LENGTH ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
	}
	
	return total_len ;
}

ssize_t _slurm_msg_sendto ( slurm_fd open_fd, char *buffer , size_t size , uint32_t flags, slurm_addr * slurm_address )
{
	size_t send_len ;

	char size_buffer_temp [8] ;
	char * size_buffer = size_buffer_temp ;
	unsigned int size_buffer_len = 8 ;

	struct sigaction newaction ;
	struct sigaction oldaction ;

	newaction . sa_handler = SIG_IGN ;

	/* ignore SIGPIPE so that send can return a error code if the other side closes the socket */
	sigaction(SIGPIPE, &newaction , & oldaction );

	pack32 (  size , ( void ** ) & size_buffer , & size_buffer_len ) ;

	while ( true )
	{
		if ( ( send_len = _slurm_send ( open_fd , size_buffer_temp , sizeof ( uint32_t ) , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS) )  == SLURM_PROTOCOL_ERROR )
		{
			if ( errno ==  EINTR )
			{
				continue ;
			}
			else
			{
				sigaction(SIGPIPE, &oldaction , &newaction);
				return SLURM_PROTOCOL_ERROR ;
			}
		}
		else if ( send_len != sizeof ( uint32_t ) )
		{
			/*debug ( "Error sending length of datagram" ) ;*/
			/*debug ( "_slurm_msg_sendto only transmitted %i of %i bytes", send_len , sizeof ( uint32_t ) ) ;*/
			sigaction(SIGPIPE, &oldaction , &newaction);
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else
		{
			break ;
		}
	}
	while ( true )
	{
		if ( ( send_len = _slurm_send ( open_fd ,  buffer , size , SLURM_PROTOCOL_NO_SEND_RECV_FLAGS ) ) == SLURM_PROTOCOL_ERROR )
		{
			if ( errno ==  EINTR )
			{
				continue ;
			}
			else
			{
				sigaction(SIGPIPE, &oldaction , &newaction);
				return SLURM_PROTOCOL_ERROR ;
			}
		}
		else if ( send_len != size )
		{
			/*debug ( "_slurm_msg_sendto only transmitted %i of %i bytes", send_len , size ) ;*/
			sigaction(SIGPIPE, &oldaction , &newaction);
			slurm_seterrno ( SLURM_PROTOCOL_SOCKET_IMPL_NOT_ALL_DATA_SENT ) ;
			return SLURM_PROTOCOL_ERROR ;
		}
		else
		{
			break ;
		}
	}

	sigaction(SIGPIPE, &oldaction , & newaction );
	return send_len ;
}

int _slurm_shutdown_msg_engine ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

slurm_fd _slurm_listen_stream ( slurm_addr * slurm_address )
{
	int rc ;
	slurm_fd connection_fd ;
	const int one = 1;
	if ( ( connection_fd =_slurm_create_socket ( SLURM_STREAM ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error creating slurm stream socket: errno %i", errno ) ;
		return connection_fd ;
	}

	if ( ( rc = _slurm_setsockopt(connection_fd , SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one) ) ) ) 
	{
		debug ("setsockopt SO_REUSEADDR failed");
		return rc ;
	}

	if ( ( rc = _slurm_bind ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error binding slurm stream socket: errno %i" , errno ) ;
		return rc ;
	}

	if ( ( rc = _slurm_listen ( connection_fd , SLURM_PROTOCOL_DEFAULT_LISTEN_BACKLOG ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error listening on slurm stream socket: errno %i" , errno ) ;
		return rc ;
	}

	return connection_fd ;
}

slurm_fd _slurm_accept_stream ( slurm_fd open_fd , slurm_addr * slurm_address )
{
	size_t addr_len = sizeof ( slurm_addr ) ;
	slurm_fd connection_fd ;
	if ( ( connection_fd = _slurm_accept ( open_fd , ( struct sockaddr * ) slurm_address , & addr_len ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error accepting slurm stream socket: errno %i", errno ) ;
	}
	return connection_fd ;

}

slurm_fd _slurm_open_stream ( slurm_addr * slurm_address )
{
	int rc ;
	slurm_fd connection_fd ;
	if ( ( connection_fd =_slurm_create_socket ( SLURM_STREAM ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error creating slurm stream socket: errno %i", errno ) ;
		return connection_fd ;
	}

	if ( ( rc = _slurm_connect ( connection_fd , ( struct sockaddr const * ) slurm_address , sizeof ( slurm_addr ) ) ) == SLURM_SOCKET_ERROR )
	{
		debug ( "Error connecting on slurm stream socket: errno %i" , errno ) ;
		return rc ;
	}

	return connection_fd ;

}
	
int _slurm_get_stream_addr ( slurm_fd open_fd , slurm_addr * address )
{
	int size ;
	
	size = sizeof ( address ) ;
	return _slurm_getsockname ( open_fd , ( struct sockaddr * ) address , & size ) ;
}

int _slurm_close_stream ( slurm_fd open_fd )
{
	return _slurm_close ( open_fd ) ;
}

extern int _slurm_socket (int __domain, int __type, int __protocol)
{
	return socket ( __domain, __type, __protocol ) ;
}	

extern slurm_fd _slurm_create_socket ( slurm_socket_type_t type )
{
	switch ( type )
	{
		case SLURM_STREAM :
			return _slurm_socket ( AF_INET, SOCK_STREAM, IPPROTO_TCP) ;
			break;
		case SLURM_MESSAGE :
			return _slurm_socket ( AF_INET, SOCK_DGRAM, IPPROTO_UDP ) ;
			break;
		default :
			return SLURM_SOCKET_ERROR;
	}
}

/* Create two new sockets, of type TYPE in domain DOMAIN and using
 * protocol PROTOCOL, which are connected to each other, and put file
 * descriptors for them in FDS[0] and FDS[1].  If PROTOCOL is zero,
 * one will be chosen automatically.  Returns 0 on success, -1 for errors.  */
extern int _slurm_socketpair (int __domain, int __type, int __protocol, int __fds[2])
{
	return SLURM_PROTOCOL_FUNCTION_NOT_IMPLEMENTED ;
}

/* Give the socket FD the local address ADDR (which is LEN bytes long).  */
extern int _slurm_bind (int __fd, struct sockaddr const * __addr, socklen_t __len)
{
	return bind ( __fd , __addr , __len ) ;
}

/* Put the local address of FD into *ADDR and its length in *LEN.  */
extern int _slurm_getsockname (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len)
{
	return getsockname ( __fd , __addr , __len ) ;	
}

/* Open a connection on socket FD to peer at ADDR (which LEN bytes long).
 * For connectionless socket types, just set the default address to send to
 * and the only address from which to accept transmissions.
 * Return 0 on success, -1 for errors.  */
extern int _slurm_connect (int __fd, struct sockaddr const * __addr, socklen_t __len)
{
	return connect ( __fd , __addr , __len ) ;
}

/* Put the address of the peer connected to socket FD into *ADDR
 * (which is *LEN bytes long), and its actual length into *LEN.  */
extern int _slurm_getpeername (int __fd, struct sockaddr * __addr, socklen_t *__restrict __len)
{
	return getpeername ( __fd , __addr , __len ) ;
}

/* Send N bytes of BUF to socket FD.  Returns the number sent or -1.  */
extern ssize_t _slurm_send (int __fd, __const void *__buf, size_t __n, int __flags)
{
	return send ( __fd , __buf , __n , __flags ) ;
}

/* Read N bytes into BUF from socket FD.
 * Returns the number read or -1 for errors.  */
extern ssize_t _slurm_recv (int __fd, void *__buf, size_t __n, int __flags)
{
	return recv ( __fd , __buf , __n , __flags ) ;
}

/* Send N bytes of BUF on socket FD to peer at address ADDR (which is
 * ADDR_LEN bytes long).  Returns the number sent, or -1 for errors.  */
extern ssize_t _slurm_sendto (int __fd, __const void *__buf, size_t __n, int __flags, struct sockaddr const * __addr, socklen_t __addr_len)
{
	return sendto ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}
/* Read N bytes into BUF through socket FD.
 * If ADDR is not NULL, fill in *ADDR_LEN bytes of it with tha address of
 * the sender, and store the actual size of the address in *ADDR_LEN.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvfrom (int __fd, void *__restrict __buf, size_t __n, int __flags, struct sockaddr * __addr, socklen_t *__restrict __addr_len)
{
	return recvfrom ( __fd , __buf , __n , __flags , __addr, __addr_len) ;
}

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes sent, or -1 for errors.  */
extern ssize_t _slurm_sendmsg (int __fd, __const struct msghdr *__msg, int __flags)
{
	return sendmsg ( __fd , __msg , __flags ) ;
}

/* Send a msg described MESSAGE on socket FD.
 * Returns the number of bytes read or -1 for errors.  */
extern ssize_t _slurm_recvmsg (int __fd, struct msghdr *__msg, int __flags)
{
	return recvmsg ( __fd , __msg , __flags );
}

/* Put the current value for socket FD's option OPTNAME at protocol level LEVEL
 * into OPTVAL (which is *OPTLEN bytes long), and set *OPTLEN to the value's
 * actual length.  Returns 0 on success, -1 for errors.  */
extern int _slurm_getsockopt (int __fd, int __level, int __optname, void *__restrict __optval, socklen_t *__restrict __optlen)
{
	return getsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}

/* Set socket FD's option OPTNAME at protocol level LEVEL
 * to *OPTVAL (which is OPTLEN bytes long).
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_setsockopt (int __fd, int __level, int __optname, __const void *__optval, socklen_t __optlen)
{
	return setsockopt ( __fd , __level , __optname , __optval , __optlen ) ;
}


/* Prepare to accept connections on socket FD.
 * N connection requests will be queued before further requests are refused.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_listen (int __fd, int __n)
{
	return listen ( __fd , __n ) ;
}

/* Await a connection on socket FD.
 * When a connection arrives, open a new socket to communicate with it,
 * set *ADDR (which is *ADDR_LEN bytes long) to the address of the connecting
 * peer and *ADDR_LEN to the address's actual length, and return the
 * new socket's descriptor, or -1 for errors.  */
extern int _slurm_accept (int __fd, struct sockaddr * __addr, socklen_t *__restrict __addr_len)
{
	return accept ( __fd , __addr , __addr_len ) ;
}

/* Shut down all or part of the connection open on socket FD.
 * HOW determines what to shut down:
 * SHUT_RD   = No more receptions;
 * SHUT_WR   = No more transmissions;
 * SHUT_RDWR = No more receptions or transmissions.
 * Returns 0 on success, -1 for errors.  */
extern int _slurm_shutdown (int __fd, int __how)
{
	return shutdown ( __fd , __how );
}

extern int _slurm_close (int __fd )
{
	return close ( __fd ) ;
}

extern int _slurm_select(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
	return select ( n , readfds , writefds , exceptfds , timeout ) ;
}
/*
   extern int _slurm_pselect(int n, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, sigset_t * sigmask)
   {
   return pselect ( n , readfds , writefds , exceptfds , timeout , sigmask ) ;
   }
   */
extern void _slurm_FD_CLR(int fd, fd_set *set)
{
	FD_CLR ( fd , set ) ;
}
extern int _slurm_FD_ISSET(int fd, fd_set *set)
{
	return FD_ISSET ( fd , set ) ;
}
extern void _slurm_FD_SET(int fd, fd_set *set)
{
	FD_SET ( fd , set ) ;
}
extern void _slurm_FD_ZERO(fd_set *set)
{
	FD_ZERO ( set ) ;
}

extern int _slurm_fcntl(int fd, int cmd)
{
	return fcntl ( fd , cmd ) ;
}
/*
   extern int _slurm_fcntl(int fd, int cmd, long arg)
   {
   }
   extern int _slurm_fcntl(int fd, int cmd, struct flock *lock)
   {
   }
   extern int _slurm_ioctl(int d, int request, ...)
   {
   return ioctl ( d , request, ... ) ;
   }
   */

/* sets the fields of a slurm_addr */
void _slurm_set_addr_uint ( slurm_addr * slurm_address , uint16_t port , uint32_t ip_address )
{
	slurm_address -> sin_family = AF_SLURM ;
	slurm_address -> sin_port = htons ( port ) ;
	slurm_address -> sin_addr.s_addr = htonl ( ip_address ) ;
}

/* sets the fields of a slurm_addr */
void _slurm_set_addr ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	_slurm_set_addr_char ( slurm_address , port , host ) ;		
}
void _slurm_set_addr_char ( slurm_addr * slurm_address , uint16_t port , char * host )
{
	struct hostent * host_info = gethostbyname ( host ) ;
	if (host_info == NULL) {
		error ("gethostbyname failure on %s, defaulting to localhost", host);
		host_info = gethostbyname ( "localhost" ) ;
	}
	memcpy ( & slurm_address -> sin_addr . s_addr , host_info -> h_addr , host_info -> h_length ) ;
	slurm_address -> sin_family = AF_SLURM ;
	slurm_address -> sin_port = htons ( port ) ;
}

void _slurm_get_addr ( slurm_addr * slurm_address , uint16_t * port , char * host , unsigned int buf_len )
{
	struct hostent * host_info = gethostbyaddr ( ( char * ) &( slurm_address -> sin_addr . s_addr ) , sizeof ( slurm_address ->  sin_addr . s_addr ) , AF_SLURM ) ;
	*port = slurm_address -> sin_port ;
	strncpy ( host , host_info -> h_name , buf_len ) ;
}

void _slurm_print_slurm_addr ( FILE * stream , slurm_addr * address )
{
	fprintf ( stream , "family %x\n", ntohl ( address -> sin_family ) ) ;
	fprintf ( stream , "addr %x\n", ntohl ( address -> sin_addr.s_addr ) ) ;
	fprintf ( stream , "port %x\n", ntohs ( address -> sin_port ) ) ;
}
	

void _slurm_pack_slurm_addr ( slurm_addr * slurm_address , void ** buffer , int * length )
{
	pack32 ( ntohl ( slurm_address -> sin_addr.s_addr ) , ( void ** ) buffer , length ) ;
	pack16 ( ntohs ( slurm_address -> sin_port ) , ( void ** ) buffer , length ) ;
}

void _slurm_unpack_slurm_addr_no_alloc ( slurm_addr * slurm_address , void ** buffer , int * length )
{
	slurm_address -> sin_family = AF_SLURM ;
	unpack32 ( & slurm_address -> sin_addr.s_addr , ( void ** ) buffer , length ) ;
	slurm_address -> sin_addr.s_addr = htonl ( slurm_address -> sin_addr.s_addr );
	unpack16 ( & slurm_address -> sin_port , ( void ** ) buffer , length ) ;
	slurm_address -> sin_port = htons ( slurm_address -> sin_port ) ;
}
