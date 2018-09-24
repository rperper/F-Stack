#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
 
#include <ls_sock.h>

#define DEFAULT_MAX_SOCKETS     65536
int local_accept(int s, struct sockaddr *addr, socklen_t *addrLen);
 
int ls_sock_max_sockets = DEFAULT_MAX_SOCKETS;
ls_fcntl_func       ls_fcntl_arr[2]        = { (ls_fcntl_func)fcntl, 
                                               (ls_fcntl_func)ff_fcntl };
ls_ioctl_func       ls_ioctl_arr[2]        = { (ls_ioctl_func)ioctl, 
                                               (ls_ioctl_func)ff_ioctl };
ls_getsockopt_func  ls_getsockopt_arr[2]   = { (ls_getsockopt_func)getsockopt,
                                               (ls_getsockopt_func)ff_getsockopt };
ls_setsockopt_func  ls_setsockopt_arr[2]   = { (ls_setsockopt_func)setsockopt,
                                               (ls_setsockopt_func)ff_setsockopt };
ls_listen_func      ls_listen_arr[2]       = { (ls_listen_func)listen,
                                               (ls_listen_func)ff_listen  };
ls_bind_func        ls_bind_arr[2]         = { (ls_bind_func)bind, 
                                               (ls_bind_func)ff_bind  };
ls_accept_func      ls_accept_arr[2]       = { (ls_accept_func)accept,   
                                               (ls_accept_func)local_accept };
// Connect has different POINTER parameters so is done in-line                                               
//ls_connect_func     ls_connect_arr[2]      = { (ls_connect_func)connect, 
//                                               (ls_connect_func)ff_connect };
ls_close_func       ls_close_arr[2]        = { (ls_close_func)close,   
                                               (ls_close_func)ff_close };
ls_shutdown_func    ls_shutdown_arr[2]     = { (ls_shutdown_func)shutdown,  
                                               (ls_shutdown_func)ff_shutdown };
ls_getpeername_func ls_getpeername_arr[2]  = { (ls_getpeername_func)getpeername,
                                               (ls_getpeername_func)ff_getpeername };
ls_getsockname_func ls_getsockname_arr[2]  = { (ls_getsockname_func)getsockname,
                                               (ls_getsockname_func)ff_getsockname };
ls_read_func        ls_read_arr[2]         = { (ls_read_func)read, 
                                               (ls_read_func)ff_read  };
ls_readv_func       ls_readv_arr[2]        = { (ls_readv_func)readv, 
                                               (ls_readv_func)ff_readv  };
ls_write_func       ls_write_arr[2]        = { (ls_write_func)write, 
                                               (ls_write_func)ff_write  };
ls_writev_func      ls_writev_arr[2]       = { (ls_writev_func)writev, 
                                               (ls_writev_func)ff_writev  };
ls_send_func        ls_send_arr[2]         = { (ls_send_func)send, 
                                               (ls_send_func)ff_send  };
ls_sendto_func      ls_sendto_arr[2]       = { (ls_sendto_func)sendto, 
                                               (ls_sendto_func)ff_sendto  };
ls_sendmsg_func     ls_sendmsg_arr[2]      = { (ls_sendmsg_func)sendmsg, 
                                               (ls_sendmsg_func)ff_sendmsg  };
ls_recv_func        ls_recv_arr[2]         = { (ls_recv_func)recv, 
                                               (ls_recv_func)ff_recv  };
ls_recvfrom_func    ls_recvfrom_arr[2]     = { (ls_recvfrom_func)recvfrom,
                                               (ls_recvfrom_func)ff_recvfrom };
ls_recvmsg_func     ls_recvmsg_arr[2]      = { (ls_recvmsg_func)recvmsg, 
                                               (ls_recvmsg_func)ff_recvmsg  };

int ls_sock_init(const char *conf, int argc, char * const argv[])
{
    {
        struct rlimit rlmt;
        
        ls_sock_max_sockets = 65536; // Seems silly to fail for this.
        if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            fprintf(stderr,"Error getting file limit: %s\n", strerror(errno));
        }
        else if ((rlmt.rlim_cur >= 256) && ((int)rlmt.rlim_cur < ls_sock_max_sockets))
        {
            ls_sock_max_sockets = (int)rlmt.rlim_cur;
        }
        //printf("New max sockets: %d\n", ls_sock_max_sockets);
    }
    
    return ff_init(argc, argv); // The documented api includes the conf param!
}

int local_accept(int sockfd, struct sockaddr *addr, socklen_t *addrLen)
{
    int rc;
    rc = ff_accept(sockfd, (struct linux_sockaddr *)addr, addrLen);
    if (rc < 0)
        return rc;
    return log_sockfd(rc);
}

int ls_poll1(struct pollfd fds[], nfds_t nfds, int timeout)
{
    if (nfds > 1)
    {
        errno = ERANGE;
        return -1;
    }
    if (fds[0].fd == -1)
        return 0;
    if (is_usersock(fds[0].fd))
    {
        struct pollfd fds1;
        int rc;
        fds1.fd = real_sockfd(fds[0].fd);
        fds1.events = fds[0].events;
        fds1.revents = 0;
        rc = ls_upoll(&fds1, 1, timeout);
        if (rc != 1)
            return rc;
        fds[0].revents = fds1.revents;
        return rc;
    }
    return ls_kpoll(fds, nfds, timeout);
}

int ls_socket_fstack(int domain, int type, int protocol)
{
    int rc = ff_socket(domain, type, protocol);
    if (rc >= 0)
        return log_sockfd(rc);
    return rc;
}

