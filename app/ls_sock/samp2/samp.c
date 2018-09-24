#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "ff_api.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>

struct sockaddr_in sa;
int sock;
int rc;
char buffer[2048];
int state = 0;

int loop(void *arg)
{
    if (state == 0)
    {
        printf("Doing socket call\n");
        sock = ff_socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
        {
            printf("Error in basic socket call: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        state++;
        return 0;
    }
    if (state == 1)
    {
        int on = 1;
        if (ff_ioctl(sock, FIONBIO, &on) == -1)
        {
            printf("Error setting socket non-blocking: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        state++;
        return 0;
    }
    if (state == 2)
    {
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (ff_poll(&pfd, 1, -1) == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        printf("Doing ff_connect, socket #%d\n", sock);
        if (ff_connect(sock, (const linux_sockaddr *)&sa, sizeof(sa)) == -1) 
        {
            printf("Error in connect: %s\n", strerror(errno));
            ff_close(sock);
            exit(1);
        }
        state++;
        return 0;
    }
    if (state == 3)
    {
        printf("Doing ff_send\n");
        snprintf(buffer, sizeof(buffer) - 3, "GET http://index.html\n\r");
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLOUT;
        pfd.revents = 0;
        if (ff_poll(&pfd, 1, -1) == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        rc = ff_send(sock, buffer, strlen(buffer), 0);
        if (rc == -1)
        {
            printf("Error in send: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        state++;
        return 0;
    }
    if (state == 4)
    {
        printf("Doing ff_recv\n");
        struct pollfd pfd;
        pfd.fd = sock;
        pfd.events = POLLIN;
        pfd.revents = 0;
        if (ff_poll(&pfd, 1, -1) == -1)
        {
            printf("Error in poll: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        rc = ff_recv(sock, buffer, sizeof(buffer) - 1, 0);
        if (rc == -1)
        {
            printf("Error in recv: %s\n", strerror(errno));
            ff_close(sock);
            return -1;
        }
        printf("Received: %d bytes\n", rc);
        buffer[rc] = 0;
        printf(":%s\n:\n", buffer);
        state++;
        return 0;
    }
    ff_close(sock);
    return -1; // End for good.
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
    if (ff_init(argc, argv) != 0)
    {
        printf("ff_sock_init failed, errno: %s\n", strerror(errno));
        return 1;
    }
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port   = htons(atoi(argv[2]));
    sa.sin_addr.s_addr = addr;
    ff_run(loop, NULL);
    return 0;
}
