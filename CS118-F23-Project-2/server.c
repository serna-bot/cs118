#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "utils.h"

int handle_succ_recv(struct packet* data_pkt, struct packet_queue* pkt_buf, char* str_to_write, unsigned short* order, unsigned short* last_rcv_seq, unsigned int *seq_num, unsigned int* content_length, struct sockaddr_in* client_addr_to, int send_sockfd) {
    // will be inside of a loop for recv
    //we want to continuously update the buffer and the exp seq and ack so we can send the proper ack once (cummulative ack)
    size_t str_len = (str_to_write != NULL) ? strlen(str_to_write) : 0;
    if (!data_pkt->last) {
        // handle the packets if it is not the closing packet
        printf("Expecting packet with sequence number: %u. Got %d\n", *order, data_pkt->seqnum);
        struct packet ack_pkt;
        char empty_payload[1] = "";

        if (data_pkt->last) {
            return 3;
        }

        if (data_pkt->seqnum < *order && data_pkt->acknum ) {
            // catching if a packet was lost
            printf("Seqnum of data is less than expected: %u.\n", data_pkt->seqnum );
            // build_packet(&ack_pkt, data_pkt->acknum, data_pkt->seqnum + data_pkt->length, '\0', '\0', 1, &empty_payload);
            // sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, client_addr_to, sizeof(*client_addr_to));
            // printSend(&ack_pkt, 1);
            return 4;
        }
        //received an ack so update the most current in order packet
        else if (*order == data_pkt->seqnum) {
            // is in order
            printf("Packet with seq %u was in order.", *order);
            size_t availableSpace = (*content_length) - str_len;
            // printf("data is in order: %s available space: %d data length: %d\n", data_pkt.payload, availableSpace, data_pkt.length);
            memcpy(str_to_write + str_len, data_pkt->payload, availableSpace < data_pkt->length ? availableSpace : data_pkt->length);
            str_len = strlen(str_to_write);
            // printf("curr %s\n", str_to_write);
            *order = data_pkt->seqnum + 1;
            *last_rcv_seq = data_pkt->acknum;
            // printf("checking pkt buffer %d\n", pkt_buf->count);

            //now check if the buffer has items and see if the top item is the next item
            while (!queue_empty(pkt_buf) && pkt_buf->front->curr->seqnum == *order) {
                struct packet* temp = dequeue(pkt_buf, NULL, 0);
                *order = temp->seqnum + 1;
                *last_rcv_seq = temp->acknum;
                availableSpace = content_length - strlen(str_to_write) - 1;
                memcpy(str_to_write + str_len, temp->payload, availableSpace < temp->length ? availableSpace : temp->length);
                str_len = strlen(str_to_write);
                free(temp); //free temp since underneath the packet was allocated using malloc
            }
            struct packet ack_pkt;
            char empty_payload[1] = "";
            build_packet(&ack_pkt, (*seq_num)++, *order, '\0', '\0', 1, &empty_payload);
            sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, client_addr_to, sizeof(*client_addr_to));
            printSend(&ack_pkt, 0);
            return 1;
            // build_packet(ack_pkt, data_pkt.acknum, order, '\0', '\0', 1, &empty_payload);
        }
        else {
            // packet was out of order
            // build_packet(ack_pkt, last_in_order_seqnum + 1, order, '\0', '\0', 1, &empty_payload);
            printf("Packet was out of order. Expecting %u but got %u.\n", *order, data_pkt->seqnum);
            if (!find_queue(pkt_buf, data_pkt->seqnum)) enqueue(pkt_buf, data_pkt, 1);
            return 2;
        }
            
            // sendto(send_sockfd, ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
            // printSend(ack_pkt, 0);

            // the seqnum being sent by the server is wrong
            
    }
    else {
        //signifies that the client is done
        while (!queue_empty(pkt_buf) && pkt_buf->front->curr->seqnum == order) {
            struct packet* temp = dequeue(&pkt_buf, NULL, 0);
            *order = temp->seqnum + (unsigned short)temp->length;
            size_t availableSpace = content_length - strlen(str_to_write) - 1;
            strncat(str_to_write, temp->payload, availableSpace); //write to the string

            free(temp); //free temp since underneath the packet was allocated using malloc

        }
        //closing packet
        // struct packet ack_pkt;
        // char empty_payload[1] = "";
        // build_packet(&ack_pkt, data_pkt->acknum + 1, data_pkt->seqnum, '\0', '1', 1, &empty_payload);
        // sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
        // printSend(&ack_pkt, 0);
        return 3;
    }

    return -1;
}

int main() {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in server_addr, client_addr_from, client_addr_to;
    struct packet buffer;
    socklen_t addr_size = sizeof(client_addr_from);
    int expected_seq_num = 0;
    int recv_len;
    struct packet ack_pkt;

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Configure the server address structure
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the server address
    if (bind(listen_sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Configure the client address structure to which we will send ACKs
    memset(&client_addr_to, 0, sizeof(client_addr_to));
    client_addr_to.sin_family = AF_INET;
    client_addr_to.sin_addr.s_addr = inet_addr(LOCAL_HOST);
    client_addr_to.sin_port = htons(CLIENT_PORT_TO);

    // Open the target file for writing (always write to output.txt)
    FILE *fp = fopen("output.txt", "wb");

    // TODO: Receive file from the client and save it as output.txt
    struct packet_queue pkt_buf;
    init_packet_queue(&pkt_buf);
    
    // need to change the listen_sockfd to nonblocking since ack can be lost and need to handle the timeout
    // window size can stay as one though (since we dont want race conditions when we write to the output file)
    
    unsigned short order = 0; 
    
    // we are assuming that we are always going to get the first packet (so use blocking socket) 
    // since if it gets lost, it will timeout and retransmit
    
    char *str_to_write = NULL;
    // size_t str_len = 0;
    unsigned int content_length = 0;
    unsigned int seq_num = 0, last_seq_num = 0;
    int cont_flag = 1, rcv_first_pkt = 0;

    int flags = fcntl(listen_sockfd, F_GETFL, 0);
    fcntl(listen_sockfd, F_SETFL, flags | O_NONBLOCK | O_NDELAY);

    fd_set ready_fds;
    struct timeval timeout;
    int max_fd;
    
    FD_ZERO(&ready_fds);
    max_fd = listen_sockfd + 1;
    FD_SET(listen_sockfd, &ready_fds);
    
    //write to file
    while(1) {
        // Set the timeout for select
        fd_set ready_fds;
        struct timeval timeout;
        int max_fd;
        
        FD_ZERO(&ready_fds);
        max_fd = listen_sockfd + 1;
        FD_SET(listen_sockfd, &ready_fds);
        timeout.tv_sec = TIMEOUT;
        timeout.tv_usec = 0;

        int ready = rcv_first_pkt ? select(max_fd, &ready_fds, NULL, NULL, &timeout) : select(max_fd, &ready_fds, NULL, NULL, NULL);

        if (ready == -1) {
            perror("select error.");
        }
        else if (ready > 0 && FD_ISSET(listen_sockfd, &ready_fds)) {
            struct packet data_pkt;
            ssize_t bytes_rcv = recvfrom(listen_sockfd, &data_pkt, sizeof(struct packet), 0, (struct sockaddr *)&client_addr_from, &addr_size);
            // if (bytes_rcv < 0) perror("recvfrom");
            if (bytes_rcv > 0) {
                printRecv(&data_pkt);
                if (data_pkt.seqnum == 0 && str_to_write == NULL) {
                    // printf("payload: %s", data_pkt.payload);
                    sscanf(data_pkt.payload, "Content Length: %u\n", &content_length);
                    // printf("allocating mem of size: %u", content_length);
                    str_to_write = calloc(content_length + 1, sizeof(char));

                    struct packet ack_pkt;
                    char empty_payload[1] = "";
                    build_packet(&ack_pkt, seq_num++, data_pkt.seqnum + 1, '\0', '\0', 1, &empty_payload);
                    sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
                    printSend(&ack_pkt, 0);
                    order++;
                    rcv_first_pkt = 1;
                    continue;
                }
                int res = handle_succ_recv(&data_pkt, &pkt_buf, str_to_write, &order, &last_seq_num, &seq_num, &content_length, &client_addr_to, send_sockfd);
                
                if (res < 0 ) {
                    perror("something went wrong");
                }
                else if (res == 3) {
                    struct packet ack_pkt;
                    char empty_payload[1] = "";
                    build_packet(&ack_pkt, seq_num++, order, '\0', '1', 1, &empty_payload);
                    sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
                    printSend(&ack_pkt, 0);
                    break;
                }
            }
        }
        else if (rcv_first_pkt == 1 && ready == 0) {
            //signals that the socket timed out and therefore we can send the ack 
            struct packet ack_pkt;
            char empty_payload[1] = "";
            build_packet(&ack_pkt, seq_num - 1, order, '\0', '\0', 1, &empty_payload);
            sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
            printSend(&ack_pkt, 1);
        }
    }
    // printf("output: %s\n", str_to_write);
    size_t bytesWritten = fwrite(str_to_write, sizeof(char), strlen(str_to_write), fp);

    if (bytesWritten != strlen(str_to_write)) perror("write error.");
    if (str_to_write) free(str_to_write);
    else perror("header was not sent.");
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
