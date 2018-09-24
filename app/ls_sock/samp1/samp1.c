#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <ls_sock.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int main(int argc, char *argv[])
{
    in_addr_t addr;
    struct sockaddr_in sa;
    int sock;
    int rc;
    char buffer[2048];

    printf("Test simple connect, receive and disconnect using library and kernel sockets\n");
    printf("Parameters: samp1 <ipaddr> <port>\n");
    printf("I test with: ./samp1 192.168.0.204 80\n");
     
    if (argc != 3)
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
    printf("Doing socket call\n");
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1)
    {
        printf("Error in basic socket call: %s\n", strerror(errno));
        return 1;
    }
    printf("Doing ls_connect\n");
    if (ls_connect(sock, (const struct sockaddr *)&sa, sizeof(sa)) == -1) 
    {
        printf("Error in connect: %s\n", strerror(errno));
        ls_close(sock);
        return 1;
    }
    printf("Doing ls_send\n");
    snprintf(buffer, sizeof(buffer) - 3, "GET http://index.html\n\r");
    rc = ls_send(sock, buffer, strlen(buffer), 0);
    if (rc == -1)
    {
        printf("Error in send: %s\n", strerror(errno));
        ls_close(sock);
        return 1;
    }
    printf("Doing ls_recv\n");
    rc = ls_recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (rc == -1)
    {
        printf("Error in recv: %s\n", strerror(errno));
        ls_close(sock);
        return 1;
    }
    printf("Received: %d bytes\n", rc);
    buffer[rc] = 0;
    printf(":%s\n:\n", buffer);
    ls_close(sock);
    return 0;
}
