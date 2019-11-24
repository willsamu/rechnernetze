#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/nameser_compat.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <inttypes.h>
#include "protocol.h"
#include "src/uthash.h"

typedef struct my_peer
{
    uint16_t id;
    uint32_t ip;
    uint16_t port;
} Peer;

typedef struct my_peerdata
{
    Peer predecessor;
    Peer self;
    Peer successor;

} PeerData;

typedef struct my_struct
{
    void *key;
    void *value;
    uint16_t keyLength;
    uint32_t valueLength;
    UT_hash_handle hh;
    // makes this structure hashable

} User;

PeerData peerdata; //initialize global variable for peer information

User *users = NULL;

User *find_user(Body *body, Header *header)
{
    User *user = NULL;
    HASH_FIND_BYHASHVALUE(hh, users, body->key, header->keyLength, 0, user);
    return user;
}
void delete_user(Body *body, Header *header)
{
    User *user = NULL;
    user = find_user(body, header);
    if (user != NULL)
    {
        HASH_DEL(users, user);
        free(user->key);
        free(user->value);
        free((user));
    }
}
void add_user(Body *body, Header *header)
{
    if (find_user(body, header) == NULL)
    {
        User *newUser = malloc(sizeof(User));
        newUser->key = body->key;
        newUser->value = body->value;
        newUser->keyLength = header->keyLength;
        newUser->valueLength = header->valueLength;
        HASH_ADD_KEYPTR_BYHASHVALUE(hh, users, newUser->key, newUser->keyLength, 0, newUser);
    }
}
void deleteAll()
{
    for (User *s = users; s != NULL; s->hh.next)
    {
        HASH_DEL(users, s);
        free(s->key);
        free(s->value);
        free((s));
    }
}
void sendDelete(int *socket)
{
    uint8_t info;
    uint16_t keyLength;
    uint32_t valueLength;
    info = 9;
    valueLength = 0;
    keyLength = 0;
    sendData(socket, (void *)(&info), 1);
    sendData(socket, (void *)(&keyLength), 2);
    sendData(socket, (void *)(&valueLength), 4);
}
void sendSet(int *socket)
{
    uint8_t info;
    uint16_t keyLength;
    uint32_t valueLength;
    info = 10;
    valueLength = 0;
    keyLength = 0;
    sendData(socket, (void *)(&info), 1);
    sendData(socket, (void *)(&keyLength), 2);
    sendData(socket, (void *)(&valueLength), 4);
}
void sendGet(int *socket, User *user)
{
    uint8_t info = 4; // in case the key ist not in the hash table (the AKC Bit is not set )
    uint16_t keyLength = 0;
    uint32_t valueLength = 0;
    void *key = NULL;
    void *value = NULL;
    if (user != NULL)
    {
        key = user->key;
        value = user->value;
        info = 12;
        keyLength = htons(user->keyLength);
        valueLength = htonl(user->valueLength);
    }
    sendData(socket, (void *)(&info), 1);
    sendData(socket, (void *)(&keyLength), 2);
    sendData(socket, (void *)(&valueLength), 4);
    if (user != NULL)
    {
        sendData(socket, key, user->keyLength);
        sendData(socket, value, user->valueLength);
        printf("SEND GET! Clients turn now..\n");
    }
}
void sendRequest(int *socket, Body *body, Header *header)
{
    if (header->info == 1)
    {
        delete_user(body, header);
        sendDelete(socket);
        return;
    }
    else if (header->info == 2)
    {
        add_user(body, header);
        sendSet(socket);
    }
    else if (header->info == 4)
    {
        printf("GET!\n");
        User *user = find_user(body, header);
        sendGet(socket, user);
    }
}

{
    int createSocket(uint32_t ip, uint16_t port, int clientOrServer) int sockfd = 0, sockserv = 0;
    struct addrinfo hints;
    struct addrinfo *servinfo, *result;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    char connectIp[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(ip), connectIp, INET_ADDRSTRLEN);
    char connectPort[16];
    sprintf(connectPort, "%d", port);

    int s = getaddrinfo(connectIp, connectPort, &hints, &servinfo);
    if (s != 0)
    {
        fprintf(stderr, "getadressinfo:%s\n", gai_strerror(s));
    }

    //? 0 to create Client Socket, 1 for Server
    switch (clientOrServer)
    {
    case 0:
        for (result = servinfo; result != NULL; result = result->ai_next)
        {
            sockfd = socket(result->ai_family, result->ai_socktype,
                            result->ai_protocol);
            if (sockfd == -1)
            {
                close(sockfd);
                continue;
            }
            if (connect(sockfd, result->ai_addr, result->ai_addrlen) != -1)
            {
                break;
            }
        }
        break;
    case 1:
        for (result = servinfo; result != NULL; result = result->ai_next)
        {
            sockserv = socket(result->ai_family, result->ai_socktype,
                              result->ai_protocol);
            if (sockserv == -1)
            {
                close(sockserv);
                continue;
            }

            if (bind(sockserv, result->ai_addr, result->ai_addrlen) >= 0)
            {
                break;
            }
        }
        break;

    default:
        break;
    }

    freeaddrinfo(servinfo);
    if (result == NULL)
    {
        fprintf(stderr, "%s\n", strerror(errno));
        return (1);
    }
    printf("returning socket... %d\n", sockfd);

    return sockfd > sockserv ? sockfd : sockserv;
}

int sendControl(Control *reply, Peer target)
{

    int targetfd = createSocket(target.ip, target.port, 0);

    // uint16_t hashId;
    // uint16_t nodeId;
    // uint32_t nodeIp;
    // uint16_t nodePort;

    uint16_t hashId = htons(reply->hashId);
    uint16_t nodeId = htons(reply->nodeId);
    uint32_t nodeIp = htonl(reply->nodeIp);
    uint16_t nodePort = htons(reply->nodePort);

    sendData(&targetfd, &(reply->info), sizeof(uint8_t));
    sendData(&targetfd, &(hashId), sizeof(uint16_t));
    sendData(&targetfd, &(nodeId), sizeof(uint16_t));
    sendData(&targetfd, &(nodeIp), sizeof(uint32_t));
    sendData(&targetfd, &(nodePort), sizeof(uint16_t));

    close(targetfd);
    return 0;
}

void handleRequest(int *new_sock, Info *info) //passing adress of objects
{

    //? Case Lookup
    if (info->info == 129)
    {
        Control *control = recvControl(new_sock, info);
        if (control != NULL)
        {

            if (control->hashId > peerdata.self.id && control->hashId < peerdata.successor.id || control->hashId > peerdata.self.id && control->hashId > peerdata.successor.id && peerdata.successor.id < peerdata.self.id)
            {
                //TODO: SEND REPLY WITH DATA OF SUCCESSOR

                Peer target = {(uint16_t)0, control->nodeIp, control->nodePort};

                Control *reply = malloc(sizeof(Control));
                reply->info = (uint8_t)130;
                reply->hashId = control->hashId;
                reply->nodeId = peerdata.successor.id;
                reply->nodeIp = peerdata.successor.ip;
                reply->nodePort = peerdata.successor.port;

                if (sendControl(reply, target) == 0)
                {
                    printf("Succesfully sent reply!");
                }
                else
                {
                    fprintf(stderr, "%s/n", strerror(errno));
                }

                free(reply);
            }
            else if (control->hashId > peerdata.successor.id)
            {
                //TODO: FORWARD TO SUCCESSOR
                if (sendControl(control, peerdata.successor) == 0)
                {
                    printf("Succesfully forwarded lookup!");
                }
                else
                {
                    fprintf(stderr, "%s/n", strerror(errno));
                }
            }
        }
        free(control);
    }

    //? Case Reply for own Lookup
    else if (info->info == 130)
    {
        // TODO: HANDLE REPLY
        // Control *control = recvControl(new_sock, info);
    }

    //? Case request (GET/SET/DELETE)
    else
    {
        Header *header = rcvHeader(new_sock, info); // to receive the data from Header
        if (header != NULL)
        {
            printHeader(header);
            Body *body = readBody(new_sock, header);
            printf("\nHeader data revieced! Body Value Size: %d \n", sizeof(body->value));
            if (body != NULL)
            {
                sendRequest(new_sock, body, header); // if the receive of the data of the header succeed , read the data of Body
                free(body);
            }
            free(header);
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 10)
    {
        fprintf(stderr, "zu wenig argumente!\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "server running\n");

    // initialize peer
    struct sockaddr_in sa;
    inet_pton(AF_INET, argv[2], &(sa.sin_addr));
    Peer pre = {(uint16_t)atoi(argv[1]), sa.sin_addr.s_addr, (uint16_t)atoi(argv[3])};
    inet_pton(AF_INET, argv[5], &(sa.sin_addr));
    Peer self = {(uint16_t)atoi(argv[4]), sa.sin_addr.s_addr, (uint16_t)atoi(argv[6])};
    inet_pton(AF_INET, argv[8], &(sa.sin_addr));
    Peer suc = {(uint16_t)atoi(argv[7]), sa.sin_addr.s_addr, (uint16_t)atoi(argv[9])};

    peerdata.predecessor = pre;
    peerdata.self = self;
    peerdata.successor = suc;

    // ! START OF Socket Connections
    fd_set master, read_fds;
    // fd_set read_fds;
    int fdmax;

    socklen_t addr_size;
    struct sockaddr_storage their_addr;
    int new_sock;
    int i, j, rv;

    int listener = createSocket(peerdata.self.ip, peerdata.self.port, 1);

    // 10 connections allowed in this case
    if (listen(listener, 10) == -1)
    {
        fprintf(stderr, "%s/n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    FD_SET(listener, &master);
    fdmax = listener;

    while (1)
    {
        read_fds = master;
        if (select(fdmax + 1, &read_fds, NULL, NULL, NULL) == -1)
        {
            perror("select");
            exit(4);
        }

        for (i = 3; i <= fdmax; i++)
        {
            if (FD_ISSET(i, &read_fds))
            {
                if (i == listener)
                {
                }
            }
        }
    }

    while (1)
    {
        addr_size = sizeof their_addr;
        new_sock = accept(listener, (struct sockaddr *)&their_addr, &addr_size);
        printf("got a socket!");
        if (new_sock == -1)
        {
            fprintf(stderr, "%s/n", strerror(errno));
        }

        Info *info = recvInfo(&new_sock);
        handleRequest(&new_sock, info);
        close(new_sock);
    }

    deleteAll();

    return 0;
}
