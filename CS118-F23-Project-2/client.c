#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include "utils.h"

//returns if client is done or not
int handle_successful_recv (struct packet* ack_pkt, struct packet_queue* pkt_queue, int last_seqnum, int *window_sze, int *dupe_acks, unsigned int *exp_seq, unsigned int *exp_ack, struct packet* last_pkt, struct sockaddr_in* server_addr_to, int send_sockfd) {

    if (ack_pkt->ack) {
        printf("Received closing ACK. Exiting loop.\n");
        return 0;
    }
    else if (ack_pkt->acknum == last_seqnum) {
        struct packet* temp = dequeue(pkt_queue, ack_pkt, 0);
        if (temp) free(temp);
        sendto(send_sockfd, last_pkt, sizeof(struct packet), 0, server_addr_to, sizeof(*server_addr_to));
        printSend(last_pkt, 0);
        enqueue(pkt_queue, last_pkt, 0);
        *window_sze = 1;
        printf("Queue size after sending last packet: %d\n", pkt_queue->count);
        return 0;
    }
    else {
        struct packet *popped_pkt = dequeue(pkt_queue, ack_pkt, 0);
        if (!popped_pkt) {
            if (ack_pkt->seqnum == *exp_seq && ack_pkt->acknum != *exp_ack) {
                //dupe ack
                if (*dupe_acks == 3) {
                    *dupe_acks = 0;
                    *window_sze = *window_sze/2 > 0 ? *window_sze/2 : 1;

                    sendto(send_sockfd, popped_pkt, sizeof(struct packet), 0, server_addr_to, sizeof(*server_addr_to));
                    enqueue(pkt_queue, popped_pkt, 1);
                    return 3; //initiate fast retransmit
                }
                ++*dupe_acks;
                return 2;
            }
            else perror("unexpected packet error");
            free(popped_pkt);
        }
        else {
            if (ack_pkt->seqnum > *exp_seq && ack_pkt->acknum > *exp_ack) {
                //cumulative ack
                struct packet* temp = dequeue(pkt_queue, ack_pkt, 1); //dequeue all before it
                if (temp) free(temp);
            }
            else if (ack_pkt->seqnum != *exp_seq && ack_pkt->acknum != *exp_ack) perror("something wrong with popped packet");
            *exp_seq = popped_pkt->acknum;
            *exp_ack = popped_pkt->seqnum + popped_pkt->length;
            free(popped_pkt);
        }
        
        ++*window_sze;
        return 1;
    }
    return -1;
}

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

    int flags = fcntl(listen_sockfd, F_GETFL, 0);
    fcntl(listen_sockfd, F_SETFL, flags | O_NONBLOCK | O_NDELAY); //making the listen socket non-blocking
    
    

    struct packet_queue pkt_queue;
    init_packet_queue(&pkt_queue);
    int window_sze = 1, j = 0, acks_rcvd = 0, dup_acks = 0;
    unsigned int exp_ack = 0, exp_seq = 0;

    // printf("packets num: %d\n", pkt_buf_sze);
    
    while ( acks_rcvd < pkt_buf_sze || !queue_empty(&pkt_queue) ) {

        //populate our sliding window (pkt_queue) change this somehow to deal with data if we are retransitting
        printf("STRTING ANOTHER LOOP! window size: %d ______________________\n", window_sze);
        for (int i = pkt_queue.count; j < pkt_buf_sze - 1 && i < window_sze; i++) {
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

        int pkts_in_transmission = pkt_queue.count;
        int ready = select(max_fd, &ready_fds, NULL, NULL, &timeout);

        printf("***** WAITING FOR ACKS pkts in transmission: %d ******\n", pkts_in_transmission);
        while (pkts_in_transmission > 0) {
            if (ready == -1) {
                perror("select error.");
            }
            else if (ready > 0 && FD_ISSET(listen_sockfd, &ready_fds)) {
            
                // printf("Packets in transmission: %d Transmitting pkt SEQ %d\n", pkts_in_transmission, pkt_queue.front->curr.seqnum);
                struct packet ack_pkt;
                ssize_t bytes_rcv = recvfrom(listen_sockfd, &ack_pkt, sizeof(struct packet), 0, (struct sockaddr *)&server_addr_from, &addr_size);
                // printf("Recv %d\n", bytes_rcv);
                if (bytes_rcv > 0) {
                    
                    acks_rcvd++;
                    printRecv(&ack_pkt);

                    int response = handle_successful_recv(&ack_pkt, &pkt_queue, sz + HEADER_SIZE, &window_sze, &dup_acks, &exp_seq, &exp_ack, &pkts_to_send[pkt_buf_sze - 1], &server_addr_to, send_sockfd);
                    if (response == 0) {
                        // printf("sent last one");
                        pkts_in_transmission--;
                    }
                    else if (response == 1) {
                        // printf("normal behavior");
                        pkts_in_transmission--;
                    }
                    else if (response == 2) {
                        // printf("dupe ack sent");
                    }
                    else if (response == 3) {
                        //initiate fast retransmit
                        // printf("packet was fast retransmitted");
                    }
                }
                else if (bytes_rcv < 0) {
                    // perror("recv error.");
                    break;
                }
                else {
                    // printf("Connection closed by the peer.\n");
                    break;
                }
                // printf("end handling recv\n"); 
            }
            //timeout occured (ready == 0)
            else {
                //resend only the first one
                printf("pkt timed out\n");
                if (pkt_queue.front->curr.last) break; 
                struct packet* popped_pkt = dequeue(&pkt_queue, NULL, 0);
                if (popped_pkt) {
                    sendto(send_sockfd, popped_pkt, sizeof(struct packet), 0, &server_addr_to, sizeof(server_addr_to));
                    printSend(popped_pkt, 1);
                    enqueue(&pkt_queue, popped_pkt, 0); //do not free popped_pkt since it is being reenqueued
                    // we want to change our algorithm

                    // [][][retransmitted packet]
                }
                window_sze = 1;
                //TODO: later you want ssthresh
            }
             
        }
        printf("end\n");
        
    }
    //note header + payload must be a max of 1200 **
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
