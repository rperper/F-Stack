#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include "ls_sock.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

struct sockaddr_in sa;
int port;
int sock;
int sock2;
int rc;
char buffer[2048];
char buffer2[2048];
int state = 0;
int kq;
struct kevent kevt;
struct kevent kevt_out[5];

#define STR_SOCKADDR_LEN 80
char *str_sockaddr(const struct sockaddr *addr, char *result)
{
    snprintf(result, STR_SOCKADDR_LEN, "port: %u, addr: %u.%u.%u.%u", 
             ((unsigned char *)addr)[2] * 256 + ((unsigned char *)addr)[3], 
             ((unsigned char *)addr)[4], 
             ((unsigned char *)addr)[5], 
             ((unsigned char *)addr)[6], 
             ((unsigned char *)addr)[7]);
    return result;
}

int loop(void *arg)
{
    if (state == 0)
    {
        printf("Doing socket call\n");
        sock = ls_socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
        {
            printf("Error in basic socket call: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        state++;
        return 0;
    }
    if (state == 1)
    {
        int reuse = 1;
        printf("Setting reuseaddr\n");
        if (ls_setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&reuse,
                          sizeof(int *)) == -1)
        {
            printf("Error setting reuse addr on %d: %s\n", sock, strerror(errno));
            ls_close(sock);
            return -1;
        }
        int on = 1;
        printf("Setting non-blocking\n");
        if (ls_ioctl(sock, FIONBIO, &on) == -1)
        {
            printf("Error setting socket non-blocking: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        char result[STR_SOCKADDR_LEN];
        printf("Doing bind on %s (port: %d)\n", str_sockaddr((const sockaddr *)&addr, result), port);
        if (ls_bind(sock, (const struct sockaddr *)&addr, sizeof(addr)) == -1)
        {
            printf("Error setting port (%d) to bind on: %s\n", port, strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        printf("Doing listen\n");
        if (ls_listen(sock, 5) == -1) 
        {
            printf("Error in listen: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        printf("Creating and setup events, nevent: %d\n", (int)(sizeof(kevt_out) / sizeof(struct kevent)));
        kq = ls_kqueue();
        if (kq == -1)
        {
            printf("Error creating kqueue: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        memset(&kevt,0,sizeof(kevt));
        kevt.ident = 0;
        kevt.flags = 33;//EV_ADD | EV_ENABLE;
        kevt.filter = -11;//EVFILT_READ;
        memset(&kevt_out, 0, sizeof(kevt_out));
        rc = ls_kevent(kq, kevt.ident ? &kevt : NULL, kevt.ident ? 1 : 0, 
                       kevt_out, sizeof(kevt_out) / sizeof(struct kevent), 
                       NULL/*&ts*/);
        if (rc != 0)
        {
            printf("Initial add of user event failed: rc: %d, %s\n", rc, strerror(errno));
            ls_close(sock);
            exit(1);
        }
        memset(&kevt,0,sizeof(kevt));
        kevt.ident = real_sockfd(sock);
        kevt.flags = EV_ADD | EV_ENABLE;
        kevt.filter = EVFILT_READ;
        state++;
        return 0;
    }
    if (state == 2)
    {
        struct sockaddr_in sa;
        socklen_t len = sizeof(sa);
        int rc;
        struct timespec ts;
        
        memset(&ts, 0, sizeof(ts));
        ts.tv_sec = 59;
        ts.tv_nsec = 1;
        // WARNING: ls_kevent ALWAYS IMMEDIATELY returns the number of events.
        // Which means usually 0.
        rc = ls_kevent(kq, kevt.ident ? &kevt : NULL, kevt.ident ? 1 : 0, 
                       kevt_out, sizeof(kevt_out) / sizeof(struct kevent), 
                       &ts);
        if (kevt.ident)
        {
            printf("Waiting for remote request\n");
            kevt.ident = 0;
        }
        if (rc == 0)
        {
            //printf("kevent returned 0 - nothing yet.\n");
            return 0;
        }
        if (rc == -1)
        {
            printf("Error in kevent: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);//return -1;
        }
        printf("Triggered events: %d, socket: %lu\n", rc, kevt_out[0].ident);
        printf("Doing ls_accept, socket #%d\n", sock);
        sock2 = ls_accept(sock, (struct sockaddr *)&sa, &len);
        if (sock2 == -1)
        {
            printf("Error #%d in accept: %s\n", errno, strerror(errno));
            ls_close(sock);
            exit(1);
        }
        state++;
        return 0;
    }
    if (state == 3)
    {
        int rc;
        struct pollfd pfd;
        pfd.fd = sock2;
        pfd.events = POLLIN;
        pfd.revents = 0;
        rc = ls_poll1(&pfd, 1, -1);
        if (rc == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        if (rc == 1)
            state++;
        return 0;
    }
    if (state == 4)
    {
        rc = ls_recv(sock2, buffer, sizeof(buffer) - 1, 0);
        if (rc == -1)
        {
            printf("Error in recv: %s using socket: %d\n", strerror(errno), sock2);
            ls_close(sock);
            exit(1);
        }
        if (rc > 0)
            state++;
        return 0;
    }
    if (state == 5)
    {
        printf("Received: %d bytes\n", rc);
        buffer[rc] = 0;
        printf(":%s\n:\n", buffer);
        state++;
        return 0;
    }
    if (state == 6)
    {
        int rc;
        printf("Doing ls_send\n");
        struct pollfd pfd;
        pfd.fd = sock2;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = ls_poll1(&pfd, 1, -1);
        if (rc == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ls_close(sock);
            return -1;
        }
        if (rc == 1)
            state++;
        return 0;
    }
    if (state == 7)
    {
        snprintf(buffer, sizeof(buffer), "<html>This is a simple response\n\r</html>\n\r");
        snprintf(buffer2, sizeof(buffer2), 
                 "HTTP/1.1 200 OK\n\rContent-Length: %lu\n\rContent-Type: text/html\n\rConnection: close\n\r\n\r%s",
                 strlen(buffer), buffer);
        printf("The full final response:\n%s", buffer2);
        rc = ls_send(sock2, buffer2, strlen(buffer2), 0);
        if (rc == -1)
        {
            printf("Error in recv: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        state++;
        return 0;
    }
    if (state == 8)
    {
        printf("shutdown returned %d\n", ls_shutdown(sock2, SHUT_RDWR));
        printf("close returned %d\n",ls_close(sock2));
        memset(&kevt,0,sizeof(kevt));
        kevt.ident = 0;
        kevt.flags = EV_DELETE;
        kevt.filter = -11;//EVFILT_READ;
        memset(&kevt_out, 0, sizeof(kevt_out));
        printf("Delete of user event returned: %d\n",
               ls_kevent(kq, &kevt, 1, 
                         kevt_out, sizeof(kevt_out) / sizeof(struct kevent), 
                         NULL/*&ts*/));
        memset(&kevt,0,sizeof(kevt));
        kevt.ident = real_sockfd(sock);
        kevt.flags = EV_DELETE;
        kevt.filter = EVFILT_READ;
        printf("Delete of socket read event returned: %d\n",
               ls_kevent(kq, &kevt, 1, 
                         kevt_out, sizeof(kevt_out) / sizeof(struct kevent), 
                         NULL/*&ts*/));
        
        printf("close of listening socket returned %d, %s\n",ls_close(sock), strerror(errno));
        printf("Normal exit!\n");
        exit(0);
    }
    return -1; // End for good.
}


int main(int argc, char * const argv[])
{
    printf("Test accept, receive and disconnect using user sockets\n");
    printf("Parameters: samp <port> -c <f-stack config file>\n");
    printf("I test with: ./samp 80 -c /usr/local/nginx_fstack/conf/f-stack.conf\n");
    printf("WARNING: Run /home/user/dpdk-start.sh to load the user-land driver\n");
    if (argc < 3)
    {
        printf("Invalid number of parameters\n");
        return 1;
    }
    port   = atoi(argv[1]);
    if (ls_sock_init(NULL, argc, argv) != 0)
    {
        printf("ls_sock_init failed, errno: %s\n", strerror(errno));
        return 1;
    }
    ls_run(loop, NULL);
    return 0;
}
