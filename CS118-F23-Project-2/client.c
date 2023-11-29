#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include "utils.h"


int main(int argc, char *argv[]) {
    int listen_sockfd, send_sockfd;
    struct sockaddr_in client_addr, server_addr_to, server_addr_from;
    socklen_t addr_size = sizeof(server_addr_to);
    struct timeval tv;
    struct packet pkt;
    struct packet ack_pkt;
    char buffer[PAYLOAD_SIZE];
    unsigned short seq_num = 0;
    unsigned short ack_num = 0;
    char last = 0;
    char ack = 0;

    // read filename from command line argument
    if (argc != 2) {
        printf("Usage: ./client <filename>\n");
        return 1;
    }
    char *filename = argv[1];

    // Create a UDP socket for listening
    listen_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (listen_sockfd < 0) {
        perror("Could not create listen socket");
        return 1;
    }

    // Create a UDP socket for sending
    send_sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_sockfd < 0) {
        perror("Could not create send socket");
        return 1;
    }

    // Configure the server address structure to which we will send data
    memset(&server_addr_to, 0, sizeof(server_addr_to));
    server_addr_to.sin_family = AF_INET;
    server_addr_to.sin_port = htons(SERVER_PORT_TO);
    server_addr_to.sin_addr.s_addr = inet_addr(SERVER_IP);

    // Configure the client address structure
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(CLIENT_PORT);
    client_addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // Bind the listen socket to the client address
    if (bind(listen_sockfd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
        perror("Bind failed");
        close(listen_sockfd);
        return 1;
    }

    // Open file for reading
    FILE *fp = fopen(filename, "rb");
    if (fp == NULL) {
        perror("Error opening file");
        close(listen_sockfd);
        close(send_sockfd);
        return 1;
    }

    // TODO: Read from file, and initiate reliable data transfer to the server

    //getting total size of the file
    fseek(fp, 0L, SEEK_END);
    int sz = ftell(fp);
    fseek(fp, 0L, SEEK_SET);

    //build packet queue
    int pkt_buf_sze = (int)(sz/PAYLOAD_SIZE)+3; // should be +2 for the closing packet and also the heaader packet
    struct packet pkts_to_send[pkt_buf_sze]; 
    int count = 0, k = 1;
    int last_length = 0;

    //add the header packet
    struct packet header_pkt;
    char const header_data[HEADER_SIZE];
    sprintf(header_data,"Content Length: %d\n", sz);
    build_packet(&header_pkt, 0, 0, '\0', '\0', HEADER_SIZE, &header_data);
    pkts_to_send[0] = header_pkt;

    for (int i = 1; i < pkt_buf_sze - 1 && count < sz; i++) {
        struct packet curr_pkt;
        if (sz - count < PAYLOAD_SIZE) last_length = sz-count;
        else last_length = PAYLOAD_SIZE;
        process_input_packets(&curr_pkt, fp, count, k, last_length);
        pkts_to_send[i] = curr_pkt;
        count += last_length;
        k ++; //for the ACK
    }
    //add the closing packet
    struct packet closing_pkt;
    char const closing_data[1] = "";
    build_packet(&closing_pkt, sz+HEADER_SIZE, k, '1', '\0', 1, &closing_data);
    pkts_to_send[pkt_buf_sze - 1] = closing_pkt;
    
    fcntl(listen_sockfd, F_GETFL, 0); //making the listen socket non-blocking
    
    int acks_rcvd = 0, dup_acks = 0;

    struct packet_queue pkt_queue;
    init_packet_queue(&pkt_queue);
    int window_sze = 1, j = 0;

    while ( acks_rcvd < pkt_buf_sze || !queue_empty(&pkt_queue) ) {

        //populate our sliding window (pkt_queue) change this somehow to deal with data if we are retransitting
        for (int i = pkt_queue.count; i < window_sze; i++) {
            sendto(send_sockfd, &pkts_to_send[j], sizeof(struct packet), 0, &server_addr_to, sizeof(server_addr_to));
            printSend(&pkts_to_send[j], 0);
            // printPayload(&pkts_to_send[j]);
            enqueue(&pkt_queue, &pkts_to_send[j], 0);
            j++;
        }
        
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
            for (int i = 0; i < window_sze; i++) {
                struct packet ack_pkt;
                ssize_t bytes_rcv = recv(listen_sockfd, &ack_pkt, sizeof(struct packet), 0);
                if (bytes_rcv > 0) {
                    printRecv(&ack_pkt);
                    while (!queue_empty(&pkt_queue) && ack_pkt.acknum > pkt_queue.front->curr.seqnum) { 
                        //indicates a retransmission occurred, whatever is less than this MUST have been transmitted successfully
                        struct packet* temp = dequeue(&pkt_queue, NULL);
                        if (temp) free(temp);
                    }
                    struct packet* temp = dequeue(&pkt_queue, &ack_pkt);
                    if (temp) {
                        free(temp);
                        acks_rcvd++;
                    }
                    else {
                        dup_acks++;
                        if (dup_acks == 3) {
                            //initiate fast retransmit
                            dup_acks = 0;
                        }
                    }
                    //upon each ack increase the window size
                    // currently assuming that the ack is never corrupted and will always be found 
                    // somewhere in the queue
                    //TODO check if dupe ACK
                }
            }
        }
        //timeout occured (ready == 0)
        else {
            //resend only the first one
            if (pkt_queue.front->curr.last) break; 
            struct packet* popped_pkt = dequeue(&pkt_queue, NULL);
            if (popped_pkt) {
                sendto(send_sockfd, popped_pkt, sizeof(struct packet), 0, &server_addr_to, sizeof(server_addr_to));
                printSend(popped_pkt, 1);
                enqueue(&pkt_queue, popped_pkt, 0); //do not free popped_pkt since it is being reenqueued
                // we want to change our algorithm

                // [][][retransmitted packet]
            }
            // window_sze = 1;
            //TODO: later you want ssthresh
        }
    }
    //note header + payload must be a max of 1200 **
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

