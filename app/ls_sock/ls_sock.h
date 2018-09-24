/*****************************************************************************
*    Open LiteSpeed is an open source HTTP server.                           *
*    Copyright (C) 2013 - 2018  LiteSpeed Technologies, Inc.                 *
*                                                                            *
*    This program is free software: you can redistribute it and/or modify    *
*    it under the terms of the GNU General Public License as published by    *
*    the Free Software Foundation, either version 3 of the License, or       *
*    (at your option) any later version.                                     *
*                                                                            *
*    This program is distributed in the hope that it will be useful,         *
*    but WITHOUT ANY WARRANTY; without even the implied warranty of          *
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the            *
*    GNU General Public License for more details.                            *
*                                                                            *
*    You should have received a copy of the GNU General Public License       *
*    along with this program. If not, see http://www.gnu.org/licenses/.      *
*****************************************************************************/

#ifndef __LS_SOCK_H__
#define __LS_SOCK_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

/**
 * @file ls_sock.h
 * @brief Include file for library which allows you to use both native and 
 * user-land sockets with the same basic API (some minor changes, see below).
 */

/**
 * @mainpage
 * liblssock
 * User reference for compatibility library for native and user-land sockets.
 * @version 1.0
 * @date 2013-2018
 * @author LiteSpeed Development Team
 * @copyright LiteSpeed Technologies, Inc. and the GNU Public License.
 */

/**
 * @def _USE_USER_SOCK       
 * @brief If defined, then user-land sockets are enabled.  
 */

#define _USE_USER_SOCK

#ifdef _USE_USER_SOCK

/**
 * @def _USE_F_STACK       
 * @brief If defined, then the f-stack user-land stack will be used.
 */
#define _USE_F_STACK

#ifdef _USE_F_STACK
#include <ff_api.h>
#endif

/**
 * @defgroup smart_macros Macros that act like functions
 * @brief The smart_macros are used like functions as they often take the place
 * of functions to decide how to handle a specific type of socket (native or
 * user-land).
 */

/**
 * @defgroup real_functions Real functions
 * @brief Actual functions that do things that are too complex to be done in a 
 * macro.          
 */

/**
 * @def errno                
 * @brief errno works like errno in kernel mode!!!
 */

/**
 * @brief ls_sock_init MUST be called before any of the functions or macros
 * in this module are used.
 * @ingroup real_functions
 * @param[in] conf A configuration file name to be used for internal config.
 * @param[in] argc Command line number of parameters
 * @param[in] argv Command line parameters.
 * @return -1 for a fatal error; 0 to continue and being processing.
 */
int ls_sock_init(const char *conf, int argc, char * const argv[]);

/**
 * @brief ls_sock_loop_func is a user defined loop function where you do all   
 * of the operations of the program (in f-stack).
 * @ingroup smart_macros
 * @param[in] arg  The argument passed in during the ls_sock_run.
 * @return int.                              
 */

typedef int (*ls_sock_loop_func)(void *arg);

/**
 * @brief ls_sock_run Does the activity of the program and calls back your loop
 * function.                                   
 * @ingroup smart_macros
 * @param[in] ls_sock_loop_func The function to be called back.
 * @param[in] *arg An optional argument.
 * @return same as for ff_run
 */

#ifdef _USE_F_STACK
#define ls_run(fn, arg) ff_run(fn, arg)
#else
#error "ls_run needs to be provided for user-land socket type"
#endif

/**
 * @brief is_usersock returns whether a logical socket is a user-land socket or 
 * a native socket.
 * @ingroup smart_macros
 * @param[in] sockfd - The logical socket to be tested.
 * @return 1 for a user-land socket; 0 for a native socket.
 */
extern int ls_sock_max_sockets;
#define is_usersock(sockfd) (int)((sockfd >= ls_sock_max_sockets) ? 1 : 0)
/**
 * @brief real_sockfd returns the actual socket that can be used with the 
 * actual call (user-land or native).
 * @ingroup smart_macros
 * @param[in] sockfd - the socket to be modified.
 * @return the modified socket.or native.       
 */
#define real_sockfd(sockfd) (int)(is_usersock(sockfd) ? sockfd - ls_sock_max_sockets : sockfd)
/**
 * @brief log_sockfd convers a user-land socket to a logical socket to be used
 * with he smart_macros in this facility.
 * @ingroup smart_macros
 * @param[in] sockfd - the user-land socket to be modified.
 * @return the logical socket.                  
 */
#define log_sockfd(sockfd)  (int)(sockfd + ls_sock_max_sockets)

/**
 * @brief ls_socket creates a user-land socket.  You MUST use the native socket
 * call to create a native socket.             
 * @ingroup real_functions
 * @param[in] same as for socket
 * @return same as for socket                
 */

#ifdef _USE_F_STACK
int ls_socket_fstack(int domain, int type, int protocol);
#define ls_socket ls_socket_fstack
#else
#error "ls_socket needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_fcntl does the type-independent fcntl
 * @ingroup smart_macros
 * @param[in] same as for fcntl
 * @return same as for fcntl                  
 */
typedef int (*ls_fcntl_func)(int sockfd, int cmd, ...);
extern ls_fcntl_func ls_fcntl_arr[];
#define ls_fcntl(a,b, ...)  ls_fcntl_arr[is_usersock(a)](real_sockfd(a), b, __VA_ARGS__)

/**
 * @brief ls_ioctl does the type-independent ioctl
 * @ingroup smart_macros
 * @param[in] same as for ioctl
 * @return same as for ioctl               
 */
typedef int (*ls_ioctl_func)(int sockfd, unsigned long request, ...);
extern ls_ioctl_func ls_ioctl_arr[];
#define ls_ioctl(a,b, ...)  ls_ioctl_arr[is_usersock(a)](real_sockfd(a), b, __VA_ARGS__)

/**
 * @brief ls_getsockopt does the type-independent getsockopt
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel           
 */
typedef int (*ls_getsockopt_func)(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
extern ls_getsockopt_func ls_getsockopt_arr[];
#define ls_getsockopt(a,b,c,d,e)  ls_getsockopt_arr[is_usersock(a)](real_sockfd(a),b,c,d,e)

/**
 * @brief ls_setsockopt does the type-independent setsockopt
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel           
 */
typedef int (*ls_setsockopt_func)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
extern ls_setsockopt_func ls_setsockopt_arr[];
#define ls_setsockopt(a,b,c,d,e)  ls_setsockopt_arr[is_usersock(a)](real_sockfd(a),b,c,d,e)

/**
 * @brief ls_listen does the type-independent listen
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_listen_func)(int sockfd, int backlog);
extern ls_listen_func  ls_listen_arr[];
#define ls_listen(a,b)  ls_listen_arr[is_usersock(a)](real_sockfd(a), b)

/**
 * @brief ls_bind does the type-independent bind.
 * @ingroup smart_macros
 * @param[in] same as for bind
 * @return same as for bind                  
 */
typedef int (*ls_bind_func)(int sockfd, const struct sockaddr *addr, socklen_t addrlen);
extern ls_bind_func  ls_bind_arr[];
#define ls_bind(a,b,c)  ls_bind_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_accept does the type-independent accept
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_accept_func)(int sockfd, struct sockaddr *addr, socklen_t *addrLen);
extern ls_accept_func  ls_accept_arr[];
#define ls_accept(a,b,c)  ls_accept_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_connect does the type-independent connect.
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return f-stack is HUGELY different (see desc below)
 * @attention f-stack requires that the socket already be set to asynchronous.
 * Then most likely it will return -1 with an errno of EINPROGRESS.  If that 
 * happens you must:
 *    - Return 0 from the processing loop
 *    - Wait for the socket to be writable (poll1 or kevent).  Return 0 until
 *      it does.
 *    - Do the following test for the final return code (set 'int err = 0'):
 * @code
 *          ls_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen)
 * @endcode
 *      If the function does not return -1, then check err.  If it returns 0
 *      then the connect worked.
 */
typedef int (*ls_connect_func)(int sockfd, const struct sockaddr *sa, socklen_t addrLen);
typedef int (*ff_connect_func)(int sockfd, const linux_sockaddr *sa, socklen_t addrLen);
#define ls_connect(a,b,c) (int)(is_usersock(a) ? ff_connect(real_sockfd(a),(const linux_sockaddr *)b,c) : connect(a,(const sockaddr *)b,c))

/**
 * @brief ls_close does the type-independent close.   
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_close_func)(int sockfd);
extern ls_close_func ls_close_arr[];
#define ls_close(a) ls_close_arr[is_usersock(a)](real_sockfd(a))

/**
 * @brief ls_shutdown does the type-independent shutdown
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_shutdown_func)(int sockfd, int how);
extern ls_shutdown_func ls_shutdown_arr[];
#define ls_shutdown(a,b)  ls_shutdown_arr[is_usersock(a)](real_sockfd(a), b)

/**
 * @brief ls_getpeername does the type-independent getpeername
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_getpeername_func)(int sockfd, struct sockaddr *addr, socklen_t *addrLen);
extern ls_getpeername_func ls_getpeername_arr[];
#define ls_getpeername(a,b,c)  ls_getpeername_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_getsockname does the type-independent getsockname
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_getsockname_func)(int sockfd, struct sockaddr *addr, socklen_t *addrLen);
extern ls_getsockname_func ls_getsockname_arr[];
#define ls_getsockname(a,b,c)  ls_getsockname_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_read does the type-independent read
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_read_func)(int sockfd, void *buf, size_t nbytes);
extern ls_read_func ls_read_arr[];
#define ls_read(a,b,c)  ls_read_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_readv does the type-independent readv
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_readv_func)(int sockfd, const struct iovec *iov, int iovcnt);
extern ls_readv_func ls_readv_arr[];
#define ls_readv(a,b,c)  ls_readv_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_write does the type-independent write
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_write_func)(int sockfd, void *buf, size_t nbytes);
extern ls_write_func ls_write_arr[];
#define ls_write(a,b,c)  ls_write_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_writev does the type-independent writev
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_writev_func)(int sockfd, const struct iovec *iov, int iovcnt);
extern ls_writev_func ls_writev_arr[];
#define ls_writev(a,b,c)  ls_writev_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_send does the type-independent send 
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_send_func)(int sockfd, void *buf, size_t nbytes, int flags);
extern ls_send_func ls_send_arr[];
#define ls_send(a,b,c,d)  ls_send_arr[is_usersock(a)](real_sockfd(a), b, c, d)

/**
 * @brief ls_sendto does the type-independent sendto
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_sendto_func)(int sockfd, const void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t toLen);
extern ls_sendto_func ls_sendto_arr[];
#define ls_sendto(a,b,c,d,e,f)  ls_sendto_arr[is_usersock(a)](real_sockfd(a), b, c, d, e, f)

/**
 * @brief ls_sendmsg does the type-independent sendmsg
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_sendmsg_func)(int sockfd, const struct msghdr *msg, int flags);
extern ls_sendmsg_func ls_sendmsg_arr[];
#define ls_sendmsg(a,b,c)  ls_sendmsg_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_recv does the type-independent recv
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_recv_func)(int sockfd, void *buf, size_t nbytes, int flags);
extern ls_recv_func ls_recv_arr[];
#define ls_recv(a,b,c,d)  ls_recv_arr[is_usersock(a)](real_sockfd(a), b, c, d)

/**
 * @brief ls_recvfrom does the type-independent recvfrom
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_recvfrom_func)(int sockfd, void *buf, size_t len, int flags, struct sockaddr *addr, socklen_t *fromLen);
extern ls_recvfrom_func ls_recvfrom_arr[];
#define ls_recvfrom(a,b,c,d,e,f)  ls_recvfrom_arr[is_usersock(a)](real_sockfd(a), b, c, d, e, f)

/**
 * @brief ls_recvmsg does the type-independent recvmsg
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef ssize_t (*ls_recvmsg_func)(int sockfd, struct msghdr *msg, int flags);
extern ls_recvmsg_func ls_recvmsg_arr[];
#define ls_recvmsg(a,b,c)  ls_recvmsg_arr[is_usersock(a)](real_sockfd(a), b, c)

/**
 * @brief ls_select and FD_* macros
 * @warning you must be careful to either use the real_sockfd socket when 
 * setting/testing a socket in FD_* macros.  They don't translate well into the
 * independent interface so no compatible functions are provided (don't use 
 * select anyway!)
 * @warning ls_select is only for user-land select.  Use regular select for 
 * the kernel sockets.
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
typedef int (*ls_select_func)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
#ifdef _USE_F_STACK
#define ls_select(a,b,c,d,e) ff_select(a,b,c,d,e)
#else
#error "ls_select needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_poll1 does the poll function for kernel and user-land fds for a
 * single file handle.
 * @warning This function is ONLY for poll length of 1.  If the poll length
 * is greater than 1, then you have to manually break it into user and kernel
 * calls and call the functions separately.
 * @ingroup real_functions
 * @param[in] same as for kernel
 * @return same as for kernel
 */
int ls_poll1(struct pollfd *fds, nfds_t, int timeout);

/**
 * @brief ls_kpoll does the poll function for kernel ONLY fds.
 * @ingroup smart_macros
 * @param[in] same as for kernel
 * @return same as for kernel
 */
#define ls_kpoll(a,b,c) poll(a,b,c)

/**
 * @brief ls_upoll does the poll function for user-land fds ONLY
 * @warning Translate your socket numbers using the real_sockfd macro as you 
 * are filling in pollfd.  Thus your test should also compare the fd as  
 * translated by read_sockfd (or convert the fd returned using log_sockfd, 
 * though this would be silly).  While the socket numbers could be converted
 * here, it is WAY more efficient to require the use of ls_poll if you're 
 * looking for a logical method of testing a socket event.  This method is
 * ONLY about speed.
 * @ingroup smart_macros    
 * @param[in] same as for kernel
 * @return same as for kernel
 */
#ifdef _USE_F_STACK
#define ls_upoll(a,b,c) ff_poll(a,b,c)
#else
#error "ls_upoll needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_epoll_create creates the fd for user-land epolls ONLY. 
 * @warning For kernel mode, use the kernel API for epoll_create and epoll_ctl.
 * @ingroup smart_macros    
 * @param[in] same as for kernel
 * @return same as for kernel
 */
#ifdef _USE_F_STACK
#define ls_epoll_create ff_epoll_create(a)
#else
#error "ls_epoll_create needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_epoll_ctl acccess the epoll fd for user-land epolls ONLY. 
 * @warning For kernel mode, use the kernel API for epoll_create and epoll_ctl.
 * @ingroup smart_macros    
 * @param[in] same as for kernel
 * @return same as for kernel
 */
#ifdef _USE_F_STACK
#define ls_epoll_ctl ff_epoll_ctl(a)
#else
#error "ls_epoll_ctl needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_kqueue Creates a kqueue handle.
 * @warning Only for user-mode sockets (no kernel mode equivalent).
 * @ingroup smart_macros    
 * @param None.                    
 * @return kqueue socket to be used with kevent.
 */
#ifdef _USE_F_STACK
#define ls_kqueue ff_kqueue
#else
#error "ls_kqueue needs to be provided for user-land socket type"
#endif

/**
 * @brief ls_kevent access a kevent using a kqueue handle.
 * @warning Only for user-mode sockets (no kernel mode equivalent).
 * @ingroup smart_macros    
 * @param[in] kq kqueue socket
 * @param[in] *changelist A list of event changes.
 * @param[in] nchanges The number of elements in changelist.
 * @param[out] *eventList A list of events which hit.
 * @param[in] nevents The size of eventList in elements.
 * @param[in] ts timespec for the wait time.  May be ignored.
 * @return -1 for an error; 0 if nothing hit; number of events in eventList
 * that hit.
 */
#ifdef _USE_F_STACK
#define ls_kevent ff_kevent
#else
#error "ls_kqueue needs to be provided for user-land socket type"
#endif

#else //!_USE_USER_SOCK
#define ls_sock_init(a,b,c)
#define ls_run(a,b)         while (1) a(b)
#define ls_bind(a,b,c)      bind(a,b,c)
#define ls_connect(a,b,c)   connect(a,b,c)
#define is_usersock(sockfd) 0
#define real_sockfd(sockfd) sockfd
#define log_sockfd(sockfd)  sockfd
#define ls_socket           socket
#define ls_socketpair       socketpair
#define ls_fcntl            fcntl
#define ls_ioctl            ioctl
#define ls_getsockopt       getsockopt
#define ls_setsockopt       setsockopt
#define ls_listen           listen
#define ls_accept           accept
#define ls_close            close
#define ls_shutdown         shutdown
#define ls_getpeername      getpeername
#define ls_getsockname      getsockname
#define ls_read             read
#define ls_readv            readv
#define ls_write            write
#define ls_writev           writev
#define ls_send             send
#define ls_sendto           sendto
#define ls_sendmsg          sendmsg
#define ls_recv             recv
#define ls_recvfrom         recvfrom
#define ls_recvmsg          recvmsg
#define ls_select           select
#define ls_poll1            poll
#define ls_kpoll            poll
#define ls_upoll            poll
#define ls_epoll_create     epoll_create
#define ls_epoll_ctl        epoll_ctl
#define ls_kqueue           #error kqueue doesnt exist for kernel functions
#define ls_kevent           #error kevent doesnt exist for kernel functions
#endif //_USE_USER_SOCK

#endif

