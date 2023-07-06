#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/signal.h>
#include "xmodemserver.h"
#include "crc16.h"
#include <sys/stat.h>

#ifndef PORT
    #define PORT 51286
#endif

#define MAXBUFFER 1024

/* Concatenate dirname and filename, and return an open
 * file pointer to file filename opened for writing in the
 * directory dirname (located in the current working directory)
 */
FILE *open_file_in_dir(char *filename, char *dirname) {
    char buffer[MAXBUFFER];
    strncpy(buffer, "./", MAXBUFFER);
    strncat(buffer, dirname, MAXBUFFER - strlen(buffer) - 1);

    // create the directory dirname; fail silently if directory exists
    if(mkdir(buffer, 0700) == -1) {
        if(errno != EEXIST) {
            perror("mkdir");
            exit(1);
        }
    }
    strncat(buffer, "/", MAXBUFFER - strlen(buffer));
    strncat(buffer, filename, MAXBUFFER - strlen(buffer));

    return fopen(buffer, "wb");
}



static int listenfd;
struct client *top; //point at the first element of the linked list

static void addclient(int fd)
{
    struct client *p = malloc(sizeof(struct client));
    if (!p) {
        fprintf(stderr, "out of memory!\n");  /* highly unlikely to happen */
        exit(1);
    }
    fflush(stdout);

    p->fd = fd;               // socket file descriptor for this client
    p->inbuf = 0;            // index into buf
    p->state = initial;  // current state of data transfer for this client
    p->current_block = 0;    // the block number of the current block
    p->next = top;  // a pointer to the next client in the list

        top = p;
}

static void removeclient(int fd)
{
    struct client **p;
    for (p = &top; *p && (*p)->fd != fd; p = &(*p)->next)
        ;
    if (*p) {
        struct client *t = (*p)->next;




        fflush(stdout);
        free(*p);
        *p = t;
    } else {
        fflush(stderr);
    }
}

void bindandlisten()  /* bind and listen, abort on error */
{
    struct sockaddr_in r;

    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket");
        exit(1);
    }

    memset(&r, '\0', sizeof r);
    r.sin_family = AF_INET;
    r.sin_addr.s_addr = INADDR_ANY;
    r.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr *)&r, sizeof r)) {
        perror("bind");
        exit(1);
    }

    if (listen(listenfd, 5)) {
        perror("listen");
        exit(1);
    }
        int num = 1;
        if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &num, sizeof(int)) == -1)
        {
        perror("setsockopt");
        }
}


void newconnection()  /* accept connection, sing to them, get response, update
                       * linked list */
{
    int fd;
        fd = accept(listenfd, NULL, NULL);
        if ((fd = accept(listenfd, NULL, NULL)) < 0){
        perror("accept");
    }
    else{
    addclient(fd);
    }
}

/*
struct client {
    int fd;               // socket file descriptor for this client
    char buf[2048];       // buffer to hold data being read from client
    int inbuf;            // index into buf
    char filename[20];    // name of the file being transferred
    FILE *fp;             // file pointer for where the file is written to
    enum recstate state;  // current state of data transfer for this client
    int blocksize;        // the size of the current block
    int current_block;    // the block number of the current block
    struct client *next;  // a pointer to the next client in the list
};*/

int main(int argc, char **argv){

    struct client *p;


    bindandlisten();  /* aborts on error */

    while (1) {
        fd_set fds;
        int maxfd = listenfd;
        FD_ZERO(&fds);
        FD_SET(listenfd, &fds);
        for (p = top; p; p = p->next) {
            FD_SET(p->fd, &fds);
            if (p->fd > maxfd)
                maxfd = p->fd;
        }
        if (select(maxfd + 1, &fds, NULL, NULL, NULL) < 0) {
            perror("select");
        } else {
            for (p = top; p; p = p->next)
                if (FD_ISSET(p->fd, &fds))
                    break;


            if (FD_ISSET(listenfd, &fds))
                newconnection();

        if(p){
            if(p->state == initial){
                int count;
                count = 0;
                char name[20];
                char *n;
                n = name;
                for(int i=0;i<20;i++){
                    if(read(p->fd, name + i, 1) > 0){
                        if(name[i] == '\r'){
                            break;
                        }
                        count = i;
                    }
                    else{
                        p->state = finished;
                        break;
                    }
                }
                if(count == 19){
                    p->state = finished;
                }
                name[count] = '\0';
                strncpy(p->filename, n, count + 1);
                p->fp = open_file_in_dir(p->filename, "filestore");
                p->state = pre_block;
                char c;
                c = 'C';
                write(p->fd, &c, sizeof(char));
            }
            if(p->state == pre_block){
                char r_character;
                char w_character;
                read(p->fd, &r_character, sizeof(char));
                if(r_character == EOT){
                    w_character = ACK;
                    write(p->fd, &w_character, sizeof(char));
                    p->state = finished;
                }
                else if(r_character == SOH){
                    p->blocksize = 132;
                    p->state = get_block;
                }
                else if(r_character == STX){
                    p->blocksize = 1028;
                    p->state = get_block;
                }
                else{
                    p->state = finished;
                }
            }
            if(p->state == get_block){
                int r = read(p->fd, p->buf + p->inbuf, p->blocksize - p->inbuf);
                p->inbuf += r;
                if(r == 0){
                    p->state = finished;
                }
                if(p->inbuf == p->blocksize){
                    p->inbuf = 0;
                    p->state = check_block;
                }

            }
            if(p->state == check_block){
                unsigned char b_num = p->buf[0];
                unsigned char inverse = p->buf[1];
                unsigned short crc = crc_message(XMODEM_KEY, (unsigned char*)&(p->buf[2]), p->blocksize - 4);
                unsigned char char_block = crc >> 8;
                unsigned char high = (p->buf[p->blocksize - 2]);
                unsigned char low = (p->buf[p->blocksize - 1]);

                char c;
                if(b_num != 255 - inverse){
                    p->state = finished;
                }
                else if (b_num == p->current_block){
                    c = ACK;
                    write(p->fd, &c, sizeof(char));
                }
                else if (high != char_block || low != crc){
                    c = NAK;
                    write(p->fd, &c, sizeof(char));
                }
                else if ((b_num != p->current_block + 1 && p->current_block != 255) || (b_num != 0 && p->current_block == 255)){
                    p->state = finished;
                }
                else{
                    c = ACK;
                    write(p->fd, &c, sizeof(char));
                    p->current_block = b_num;
                    p->inbuf = 0;
                    p->state = pre_block;

                    fwrite(p->buf + 2, sizeof(char), p->blocksize - 4, p->fp);

                }
            }
            if(p->state == finished){
                close(fileno(p->fp));
                close(p->fd);
                removeclient(p->fd);
            }
        }

        }
    }

}