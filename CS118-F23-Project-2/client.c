#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <fcntl.h>

#include "utils.h"

void send_pkts_in_queue(struct packet_queue* pkt_queue, int window_sze, struct sockaddr_in* server_addr_to, int send_sockfd) {
    struct pck_node* curr_node = pkt_queue->front;
    int i = 0;
    while (curr_node && i < window_sze) {
        sendto(send_sockfd, curr_node->curr, sizeof(struct packet), 0, server_addr_to, sizeof(*server_addr_to));
        // printSend(curr_node->curr, 0);
        curr_node = curr_node->next;
        i++;
    }
}

int add_to_win(struct packet_queue* pkt_queue, int win_sze, unsigned int last_in_order_seq, unsigned int last_ack_num, unsigned int file_sz, FILE *fp) {
    if (pkt_queue->count < win_sze) {
        unsigned int count = (last_in_order_seq - 1) * PAYLOAD_SIZE;
        unsigned int k = last_in_order_seq;
        for (int i = pkt_queue->count; i < win_sze && count < file_sz; i++) {
            struct packet curr_pkt;
            unsigned int last_length = (file_sz - count < PAYLOAD_SIZE) ? file_sz-count : PAYLOAD_SIZE;
            process_input_packets(&curr_pkt, fp, count, k++, last_ack_num++, last_length);
            enqueue(pkt_queue, &curr_pkt, 0);
            
            count += last_length;
            if (count >= file_sz ) return 0;
        }
        // print_queue(pkt_queue);
        return 1;
    }
    return -1;
}

//returns if client is done or not
int handle_successful_recv (struct packet* ack_pkt, struct packet_queue* pkt_queue, int last_seqnum, int *window_sze, int *dupe_acks, unsigned int *ack_exp_seq, unsigned int *ack_exp_ack, struct sockaddr_in* server_addr_to, int send_sockfd) {

    if (ack_pkt->ack) {
        // printf("Received closing ACK. Exiting loop.\n");
        struct packet *popped_pkt = dequeue(pkt_queue, ack_pkt, 1);
        // free(popped_pkt);
        return 0;
    }
    else if (ack_pkt->acknum == last_seqnum) {
        struct packet *popped_pkt = dequeue(pkt_queue, ack_pkt, 1);
        // free(popped_pkt);
        // printf("Queue size after dequeuing: %d\n", pkt_queue->count);
        struct packet last_pkt;
        char empty_payload[1] = "";
        build_packet(&last_pkt, (unsigned short) last_seqnum, ack_pkt->seqnum, '1', '\0', 1, &empty_payload);
        sendto(send_sockfd, &last_pkt, sizeof(struct packet), 0, server_addr_to, sizeof(*server_addr_to));
        // printSend(&last_pkt, 0);
        enqueue(pkt_queue, &last_pkt, 0);
        *window_sze = 1;
        // printf("Queue size after sending last packet: %d\n", pkt_queue->count);
        return 0;
    }
    else {
        struct packet *popped_pkt = dequeue(pkt_queue, ack_pkt, 0);
        if (!popped_pkt) {
            if (ack_pkt->acknum == *ack_exp_ack) {
                //dupe ack
                if (*dupe_acks == 3) {
                    *dupe_acks = 0;
                    *window_sze = *window_sze/2 > 0 ? *window_sze/2 : 1;


                    sendto(send_sockfd, pkt_queue->front->curr, sizeof(struct packet), 0, server_addr_to, sizeof(*server_addr_to));
                    // printSend(pkt_queue->front->curr, 1);
                    // enqueue(pkt_queue, popped_pkt, 1);
                    // free(popped_pkt);
                    return 3; //initiate fast retransmit
                }
                ++*dupe_acks;
                return 2;
            }
            // if (ack_pkt->acknum > *ack_exp_ack)
            // free(popped_pkt);
        }
        else {
            *dupe_acks = 0;
            // printf("expected ack seq_num: %u, exp ack ack_num: %u. got ack ack_num: %u\n", *ack_exp_seq, *ack_exp_ack, ack_pkt->acknum);
            // if (ack_pkt->seqnum >= *ack_exp_seq && ack_pkt->acknum >= *ack_exp_ack) {
            //     //cumulative ack
            //     struct packet* temp = dequeue(pkt_queue, ack_pkt, 1); //dequeue all before it
            //     if (temp) free(temp);
            // }
            // else if (ack_pkt->seqnum != *ack_exp_seq && ack_pkt->acknum != *ack_exp_ack) perror("something wrong with popped packet");
            *ack_exp_seq = ack_pkt->seqnum + 1;
            *ack_exp_ack = ack_pkt->acknum;
            // printf("new expected ack seq_num: %u, exp ack ack_num: %u\n", *ack_exp_seq, *ack_exp_ack);
            // free(popped_pkt);
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
    // int pkt_buf_sze = (int)(sz/PAYLOAD_SIZE)+3; // should be +2 for the closing packet and also the heaader packet
    // struct packet pkts_to_send[pkt_buf_sze]; 
    // int last_length = 0;


    int flags = fcntl(listen_sockfd, F_GETFL, 0);
    fcntl(listen_sockfd, F_SETFL, flags | O_NONBLOCK | O_NDELAY); //making the listen socket non-blocking

    struct packet_queue pkt_queue;
    init_packet_queue(&pkt_queue);

    //add the header packet
    struct packet header_pkt;
    char header_data[HEADER_SIZE];
    sprintf(&header_data,"Content Length: %d\n", sz);
    build_packet(&header_pkt, 0, 0, '\0', '\0', HEADER_SIZE, &header_data);
    enqueue(&pkt_queue, &header_pkt, 0);
    // print_queue(&pkt_queue);
    
    int window_sze = 1, j = 0, acks_rcvd = 0, dup_acks = 0;
    unsigned int pkts_to_send = sz/PAYLOAD_SIZE + 1;
    pkts_to_send = (sz % PAYLOAD_SIZE > 0) ? pkts_to_send + 1 : pkts_to_send;
    int emergency = 0;
    unsigned int last_in_order_seq = 0, ack_exp_ack = 1, ack_exp_seq = 0;
    // int count = 0;
    
    while ( ack_exp_ack <= pkts_to_send || !queue_empty(&pkt_queue) ) {

        //populate our sliding window (pkt_queue) change this somehow to deal with data if we are retransitting
        // printf("STRTING ANOTHER LOOP! window size: %d ______________________\n", window_sze);
    
        int res_add_win = add_to_win(&pkt_queue, window_sze, last_in_order_seq, ack_exp_seq, sz, fp);
        send_pkts_in_queue(&pkt_queue, window_sze, &server_addr_to, send_sockfd);
        
        int pkts_in_transmission = pkt_queue.count;

        //if == last pkt to send then send the closing ack

        // printf("***** WAITING FOR ACKS pkts in transmission: %d ******\n", pkt_queue.count);
        while (!queue_empty(&pkt_queue)) {
            // Set the timeout for select
            fd_set ready_fds;
            struct timeval timeout;
            int max_fd;
            
            FD_ZERO(&ready_fds);
            max_fd = listen_sockfd + 1;
            FD_SET(listen_sockfd, &ready_fds);

            timeout.tv_sec = TIMEOUT;
            timeout.tv_usec = 0;

            int ready = select(max_fd, &ready_fds, NULL, NULL, &timeout);
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
                    // printRecv(&ack_pkt);

                    int response = handle_successful_recv(&ack_pkt, &pkt_queue, pkts_to_send, &window_sze, &dup_acks, &ack_exp_seq, &ack_exp_ack, &server_addr_to, send_sockfd);
                    if (response == 0) {
                        // printf("sent last one");
                        ack_exp_ack = sz + HEADER_SIZE;
                        last_in_order_seq = ack_exp_ack;
                        // pkts_in_transmission--;
                    }
                    else if (response == 1) {
                        // printf("normal behavior");
                        last_in_order_seq = ack_exp_ack;
                        // pkts_in_transmission--;
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
                // printf("pkt timed out\n");
                // print_queue(&pkt_queue);
                if (pkt_queue.front->curr->last) break; 
                if (ack_exp_ack == sz + HEADER_SIZE) {
                    ack_exp_ack++;
                    break;
                } 
                struct packet* popped_pkt = dequeue(&pkt_queue, NULL, 0);
                if (popped_pkt) {
                    // sendto(send_sockfd, popped_pkt, sizeof(struct packet), 0, &server_addr_to, sizeof(server_addr_to));
                    // printSend(popped_pkt, 1);
                    enqueue(&pkt_queue, popped_pkt, 1); //do not free popped_pkt since it is being reenqueued
                    free(popped_pkt);
                    // we want to change our algorithm

                    // [][][retransmitted packet]
                }
                window_sze = 1;
                break;
            }
             
        }
        // printf("end\n");
        // if (res_add_win) {
        //     // construct and send the last ack on the next cycle
        // }
        // printf("end\n");
        
    }
    //note header + payload must be a max of 1200 **
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
