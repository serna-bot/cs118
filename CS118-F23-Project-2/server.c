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
    unsigned short order = 0;
    // need to change the listen_sockfd to nonblocking since ack can be lost and need to handle the timeout
    // window size can stay as one though (since we dont want race conditions when we write to the output file)

    while(1) {
        struct packet data_pkt;
        ssize_t bytes_rcv = recvfrom(listen_sockfd, &data_pkt, sizeof(struct packet), 0, &server_addr, sizeof(server_addr));
        if (bytes_rcv != 0) {
            if (data_pkt.last == '0') {
                if (order == data_pkt.seqnum - (unsigned short)data_pkt.length) {
                    // is in order
                    // write(fp, &data_pkt.payload); //double check this shit
                    order = data_pkt.seqnum + (unsigned short)data_pkt.length;
                    while (!queue_empty(&pkt_buf) && pkt_buf.front->curr.seqnum == order) {
                        struct packet* temp = dequeue(&pkt_buf, NULL);
                        order = temp->seqnum + (unsigned short)temp->length;
                        // write(fp, temp->payload); //double check
                        free(temp);
                    }
                }
                else {
                    enqueue(&pkt_buf, &data_pkt, 1);
                }
            }
            else {
                //signifies that the client is done
                // sendto(send_sockfd, popped_pkt, sizeof(struct packet), 0, &server_addr_to, sizeof(server_addr_to));
            }
        }
    }


    fclose(fp);
    close(listen_sockfd);
    close(send_sockfd);
    return 0;
}
