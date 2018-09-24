#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ls_sock.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

struct sockaddr_in sa;
int sock;
int rc;
char buffer[2048];
int state = 0;

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
            exit(1);
        }
        state++;
        return 0;
    }
    if (state == 1)
    {
        int on = 1;
        if (ls_ioctl(sock, FIONBIO, &on) == -1)
        {
            printf("Error setting socket non-blocking: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        state++;
        printf("Socket worked.  Now doing connect\n");
        return 0;
    }
    if (state == 2)
    {
        char result[STR_SOCKADDR_LEN];
        printf("Doing ls_connect, socket #%d, %s\n", sock, str_sockaddr((const struct sockaddr *)&sa, result));
        if (ls_connect(sock, (const struct sockaddr *)&sa, sizeof(sa)) == -1) 
        {
            if (errno == EINPROGRESS)
            {
                printf("Connect pending...go into retries\n");
                state++;
                return 0;
            }
            printf("Error in connect: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        state += 2;
        return 0;
    }
    if (state == 3)
    {
        struct pollfd pfd;
        int rc;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = ls_poll1(&pfd, 1, -1);
        if (rc == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        if ((rc == 0) || (!(pfd.revents & POLLOUT)))
            return 0; // Not ready yet.
        printf("Writable - check the return code\n");
        int err = 0;
        socklen_t errlen = sizeof(int);
        if (ls_getsockopt(sock, SOL_SOCKET, SO_ERROR, &err, &errlen) == -1) 
        {
            //if (errno == EINPROGRESS)
            //{
            //    return 0;
            //}
            printf("Error in getting getsockopt return code: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        if (err != 0)
        {
            printf("Connect error #%d\n", err);
            ls_close(sock);
            exit(1);
        }
        printf("Connected\n");
        state++;
        return 0;
    }
    if (state == 4)
    {
        printf("Doing ls_send\n");
        snprintf(buffer, sizeof(buffer) - 3, "GET http://index.html\n\r");
        struct pollfd pfd;
        int rc;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        rc = ls_poll1(&pfd, 1, -1);
        if (rc == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        if ((rc == 0) || (!(pfd.revents &POLLOUT)))
            return 0;
        rc = ls_send(sock, buffer, strlen(buffer), 0);
        if (rc == -1)
        {
            printf("Error in send: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        state++;
        return 0;
    }
    if (state == 5)
    {
        struct pollfd pfd;
        int rc = 0;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        rc = ls_poll1(&pfd, 1, -1);
        if (rc == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        if ((rc == 0) || (!(pfd.revents & POLLIN)))
            return 0;
        rc = ls_recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (rc == -1)
        {
            printf("Error in recv: %s\n", strerror(errno));
            ls_close(sock);
            exit(1);
        }
        printf("Received: %d bytes\n", rc);
        buffer[rc] = 0;
        printf(":%s\n:\n", buffer);
        state++;
        return 0;
    }
    ls_close(sock);
    exit(0); // End for good.
}


int main(int argc, char * const argv[])
{
    in_addr_t addr;

    printf("Test simple connect, receive and disconnect using user sockets\n");
    printf("Parameters: samp <ipaddr> <port> -c <f-stack config file>\n");
    printf("I test with: ./samp 192.168.0.204 80 -c /usr/local/nginx_fstack/conf/f-stack.conf\n");
    printf("WARNING: Run /home/user/dpdk-start.sh to load the user-land driver\n");
    if (argc < 3)
    {
        printf("Invalid number of parameters\n");
        return 1;
    }
    addr = inet_addr(argv[1]);
    if ((int)addr == -1)
    {
        printf("Error converting %s into an internet address: %s\n", argv[1], strerror(errno));
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(atoi(argv[2]));
    sa.sin_addr.s_addr = addr;
    if (ls_sock_init(NULL, argc, argv) != 0)
    {
        printf("ls_sock_init failed, errno: %s\n", strerror(errno));
        return 1;
    }
    ls_run(loop, NULL);
    return 0;
}
