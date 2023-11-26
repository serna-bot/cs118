#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <math.h>

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
    int pkt_buf_sze = (int)ceil((double)sz/PAYLOAD_SIZE);
    struct packet_buffer pkt_buf;
    create_buffer(&pkt_buf, pkt_buf_sze);
    int count, i;
    count = i = 0;
    while (count < sz) {
        struct packet curr_pkt;
        process_packets(&curr_pkt, fp, count, 0, PAYLOAD_SIZE);
        insert_buffer(&pkt_buf, i++, &curr_pkt);
        count += PAYLOAD_SIZE;
    }

    i = 0; // resettng counter s we can iterate through the queue

    int msec = 0, trigger = 300000; /* 5min (in ms) */
    clock_t before = clock();

    while ( msec < trigger ) {
        if (i < pkt_buf_sze) {
            struct packet* temp = get_packet(&pkt_buf, i++);
            printPayload(temp);
        }
        else {
            printf("Hello, World\n");
            break;
        }
        // iterate through the queue, send each one
        // also check for ACKs pop any from the queue if ACK comes
        // keep track of time since sent ++ msec to each one
        // any packet that is greater than timeout resend

        // char* packet_data = calloc(1, HEADER_SIZE + PAYLOAD_SIZE);
        // char* buffer = &packet_data[HEADER_SIZE];
        // memcpy(&packet_data[0], &curr_pkt.seqnum, 4);
        // memcpy(&packet_data[4], &header.acknum, 4);
        // strcpy(buffer, curr_pkt->payload);

        // sendto(send_sockfd, packet_data, HEADER_SIZE + PAYLOAD_SIZE, 0, server_addr_to);

        // free(packet_data);

    clock_t difference = clock() - before;
    msec = difference * 1000 / CLOCKS_PER_SEC;
    }

    printf("Time taken %d seconds %d milliseconds (%d iterations)\n", msec/1000, msec%1000, i);

    //note header + payload must be a max of 1200 **

    //create a timer that allows you to keep track of the packet and see if there is an ack from the server side
    
 
    
    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}

