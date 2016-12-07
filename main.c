#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include "rs232.h"

#define MAX(a,b) (((a)>(b))?(a):(b))
#define ERR_EXIT(m) \
    do { \
        perror(m); \
        exit(EXIT_FAILURE); \
    } while(0)

#define SERVER_PORT 9876
#define MAX_CLIENT_NUM 10

static volatile int running = 1;
extern int sp_fd;

void sigroutine(int dunno){
    switch(dunno){
        case SIGINT:
            running = 0;
            break;
        default:
            break;
    }
}

int open_port(char* device){
	int fd = open(device, O_RDWR | O_NOCTTY);
	if(fd==-1){
		fprintf(stderr, "open_port: Unable to open %s\n", device);
        return -1;
    }

	return (fd);
}

int main(int argc, char* argv[]){
    int maxfd = -1;
    fd_set readfds, master;
    Byte buffer[1024] = {0};

    if(argc!=2){
        fprintf(stderr, "argument required: device path\n");
        return EXIT_FAILURE;
    }

    int server_socket, client_socket[MAX_CLIENT_NUM], server_len, client_len;
    struct sockaddr_in server_address, client_address;

    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(server_socket==-1){
        fprintf(stderr, "socket:%d %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    if(setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&running, sizeof(running))==-1){
        fprintf(stderr, "setsockopt:%d %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(SERVER_PORT);
    server_len = sizeof(server_address);

    if(bind(server_socket, (struct sockaddr*)&server_address, server_len)==-1){
        fprintf(stderr, "bind:%d %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    memset(client_socket, 0, sizeof(client_socket));

    //set interrupt signal procedure
    signal(SIGINT, sigroutine);

    //serial port opening and setting
	sp_fd = open_port(argv[1]);
	if(sp_fd==-1){
        return EXIT_FAILURE;
	}

    struct termios tty;
    struct termios tty_old;
    memset(&tty, 0, sizeof(tty));
    memset(&tty_old, 0, sizeof(tty_old));

    if(tcgetattr(sp_fd, &tty)!=0){
        fprintf(stderr, "Error from tcgetattr.\n");
        close(sp_fd);
        return EXIT_FAILURE;
    }

    tty_old = tty;

    cfsetspeed(&tty, (speed_t)B115200);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;

    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;
    tty.c_cflag |= CREAD | CLOCAL;

    cfmakeraw(&tty);

    if(tcsetattr(sp_fd, TCSANOW, &tty)!=0){
        fprintf(stderr, "Error from tcsetattr.\n");
        close(sp_fd);
        return EXIT_FAILURE;
    }
    tcflush(sp_fd, TCIFLUSH);

    FD_ZERO(&master);
    FD_ZERO(&readfds);

    FD_SET(fileno(stdin), &master);
    FD_SET(sp_fd, &master);
    maxfd = sp_fd;
    FD_SET(server_socket, &master);
    maxfd = MAX(maxfd, server_socket);

    if(listen(server_socket, MAX_CLIENT_NUM)<0){
        fprintf(stderr, "listen:%d %s\n", errno, strerror(errno));
        exit(EXIT_FAILURE);
    }

    while(running){
        FD_ZERO(&readfds);
        readfds = master;

        int i;
        for(i=0;i<MAX_CLIENT_NUM;i++){
            if(client_socket[i]){
                FD_SET(client_socket[i], &readfds);
                maxfd = MAX(maxfd, client_socket[i]);
            }
        }

        memset(buffer, 0, sizeof(buffer));
        struct timeval tv;
        tv.tv_sec = 10;
        tv.tv_usec = 0;
        if(select(maxfd+1, &readfds, NULL, NULL, &tv)>0){
            if(FD_ISSET(fileno(stdin), &readfds)){
				//process user input
                int n = read(fileno(stdin), buffer, sizeof(buffer));
				if(n>0){

				}
            }else if(FD_ISSET(sp_fd, &readfds)){
				//process serial port
                ssize_t n=read(sp_fd, buffer, sizeof(buffer));
                if(n>0){
                }
            }else if(FD_ISSET(server_socket, &readfds)){
				//process socket request
                client_len = sizeof(client_address);
                int new_client_fd = accept(server_socket, (struct sockaddr *)&client_address, &client_len);
                if(new_client_fd<0){
                    fprintf(stderr, "accept:%d %s\n", errno, strerror(errno));
                    exit(EXIT_FAILURE);
                }
                for(i=0;i<MAX_CLIENT_NUM;i++){
                    if(!client_socket[i]){
                        client_socket[i] = new_client_fd;
                        break;
                    }
                }
            }else{
                for(i=0;i<MAX_CLIENT_NUM;i++){
                    if(FD_ISSET(client_socket[i], &readfds)){
                        int n = read(client_socket[i], buffer, sizeof(buffer));
                        if(n>0){
                            buffer[n] = 0;
                            RequestParser(buffer, n);
                            close(client_socket[i]);
                            client_socket[i] = 0;
                        }
                    }
                }
            }
        }
    }

    if(tcsetattr(sp_fd, TCSANOW, &tty_old)!=0){
        fprintf(stderr, "Error from tcsetattr.\n");
        close(sp_fd);
        return EXIT_FAILURE;
    }
    tcflush(sp_fd, TCIFLUSH);
    close(sp_fd);
    int i;
    for(i=0;i<MAX_CLIENT_NUM;i++){
        if(client_socket[i]>0){
            close(client_socket[i]);
            client_socket[i] = 0;
        }
    }
    close(server_socket);

    printf("\nbye...\n");

	return EXIT_SUCCESS;
}
