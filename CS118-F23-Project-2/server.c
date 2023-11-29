#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"

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
    
    char *str_to_write;
    size_t str_len = 0;
    unsigned int content_length = 0;;
    //write to file
    while(1) {
        fd_set ready_fds;
        struct timeval timeout;
        int max_fd;
        
        FD_ZERO(&ready_fds);
        max_fd = listen_sockfd + 1;
        FD_SET(listen_sockfd, &ready_fds);

        // Set the timeout for select
        timeout.tv_sec = TIMEOUT;  
        timeout.tv_usec = 0;

        int ready = select(max_fd, &ready_fds, NULL, NULL, &timeout);

        if (ready == -1) {
            perror("select error.");
        }
        else if (ready > 0 && FD_ISSET(listen_sockfd, &ready_fds)) {
            struct packet data_pkt;
            ssize_t bytes_rcv = recv(listen_sockfd, &data_pkt, sizeof(struct packet), 0);
            if (bytes_rcv < 0) perror("recvfrom");
            else if (bytes_rcv > 0) {
                printRecv(&data_pkt);
                printPayload(&data_pkt);
                if (!data_pkt.last) {
                    // handle the packets if it is not the closing packet
                    struct packet ack_pkt;
                    char empty_payload[1] = "";

                    //is the first packet
                    if (data_pkt.seqnum == 0) {
                        
                        sscanf(data_pkt.payload, "Content Length: %d\n", &content_length);
                        str_to_write = calloc(content_length+1, sizeof(char));
                        build_packet(&ack_pkt, data_pkt.acknum, data_pkt.seqnum + data_pkt.length, '\0', '\0', 1, &empty_payload);
                        sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
                        printSend(&ack_pkt, 0);
                        order += data_pkt.length;
                    }
                    else { 
                        //received an ack so send pkt 
                        if (order == data_pkt.seqnum) {
                            // is in order
                            size_t availableSpace = content_length - str_len;
                            printf("data is in order: %s available space: %d data length: %d\n", data_pkt.payload, availableSpace, data_pkt.length);
                            // strncat(str_to_write, data_pkt.payload, availableSpace); //concat string
                            memcpy(str_to_write + str_len, data_pkt.payload, availableSpace < data_pkt.length ? availableSpace : data_pkt.length);
                            printf("bhjdfhbae %s\n", str_to_write);
                            order = data_pkt.seqnum + (unsigned short)data_pkt.length;

                            //now check if the buffer has items and see if the top item is the next item
                            while (!queue_empty(&pkt_buf) && pkt_buf.front->curr.seqnum == order) {
                                struct packet* temp = dequeue(&pkt_buf, NULL);
                                order = temp->seqnum + (unsigned short)temp->length;
                                availableSpace = content_length - strlen(str_to_write) - 1;
                                // strncat(str_to_write, temp->payload, availableSpace); //write to the string
                                memcpy(str_to_write + str_len, temp->payload, availableSpace < temp->length ? availableSpace : temp->length);

                                free(temp); //free temp since underneath the packet was allocated using malloc
                            }
                        }
                        else {
                            // packet was out of order
                            enqueue(&pkt_buf, &data_pkt, 1);
                        }
                        build_packet(&ack_pkt, data_pkt.acknum, order, '\0', '\0', 1, &empty_payload);
                        sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
                        printSend(&ack_pkt, 0);
                        
                    }
                }
                else {
                    //signifies that the client is done
                    while (!queue_empty(&pkt_buf) && pkt_buf.front->curr.seqnum == order) {
                        struct packet* temp = dequeue(&pkt_buf, NULL);
                        order = temp->seqnum + (unsigned short)temp->length;
                        size_t availableSpace = content_length - strlen(str_to_write) - 1;
                        strncat(str_to_write, temp->payload, availableSpace); //write to the string

                        free(temp); //free temp since underneath the packet was allocated using malloc

                        //TODO: send the closing ack
                    }
                    struct packet ack_pkt;
                    char empty_payload[1] = "";
                    build_packet(&ack_pkt, 0, 0, '\0', '1', 1, &empty_payload);
                    sendto(send_sockfd, &ack_pkt, sizeof(struct packet), 0, &client_addr_to, sizeof(client_addr_to));
                    printSend(&ack_pkt, 0);
                    break;
                }
            }
        }
    }
    printf("output: %s\n", str_to_write);
    size_t bytesWritten = fwrite(str_to_write, sizeof(char), strlen(str_to_write), fp);

    if (bytesWritten != strlen(str_to_write)) perror("write error.");
    if (str_to_write) free(str_to_write);
    else perror("header was not sent.");
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
