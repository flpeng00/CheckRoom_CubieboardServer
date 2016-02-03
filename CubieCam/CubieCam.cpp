// SKKU ICE / EEE
// ID	  :	2009312254
// Author :	KIM DAE-HYEON
// e-mail :	flpeng00@gmail.com

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <cv.h>
#include <highgui.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <time.h>
#include <fcntl.h>
#include <termios.h>
#include <netinet/in.h>
#include <signal.h>
#include <wait.h>

#define SERVER_PORT 12000
#define BUFFER_LENGTH 50
#define SERIAL_PORT "/dev/ttyS1"
#define STX 0x02
#define ETX 0x03
#define MONITOR_MODE -1
#define STREAM_MODE -2
#define CAM_LEFT = -30;
#define CAM_RIGHT = -31

void *serverTask(void *);
void *serialTask(void *);
int configSerial(struct termios *);
int sendMessage(char *);

char serverIp[20];
int port;
int pid_stream;
int mode = MONITOR_MODE;
float t, h;

int main(int argc, char *argv[])
{
	
	CvCapture *cap = cvCaptureFromCAM(0);
	if(!cap){
		printf("CubieCam : Cannot open cam(0)\n");
		return 0;
	}

	int width = (int)cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_WIDTH);
	int height = (int)cvGetCaptureProperty(cap, CV_CAP_PROP_FRAME_HEIGHT);
	CvSize size = cvSize(width, height);
	pthread_t serverThread;
	pthread_t serialThread;
	time_t t = time(NULL);
	struct tm *tm;

	IplImage *grayImage0 = cvCreateImage(size, IPL_DEPTH_8U, 1);
	IplImage *grayImage1 = cvCreateImage(size, IPL_DEPTH_8U, 1);
	IplImage *diffImage = cvCreateImage(size, IPL_DEPTH_8U, 1);
	IplImage *frame = NULL;

	int nThreshold = 50;
	char c;
	char *diffData;
	char *tempStr;
	char filename[40];
	char date[30];
	char *buffer;
	int flag = 1;
	int i, d = 9, cnt = 0;
	int maxSize = (diffImage->widthStep) * (diffImage->height);
	float rate = 0;
	port = -1;

	printf("CubieCam : Run ServerThread\n");
	if(pthread_create(&serverThread, NULL, serverTask, NULL))
	{
		printf("CubieCam : Cannot create serverThread\n");
	}

	printf("CubieCam : Run SerialThread\n");
	if(pthread_create(&serialThread, NULL, serialTask, NULL))
	{
		printf("CubieCam : Cannot create serialThread\n");
	}

	printf("Width : %d, Height : %d, Size : %d\n", 
		diffImage->widthStep, diffImage->height, maxSize);

	printf("CubieCam : Start capturing\n");

	while(1)
	{
		frame = cvQueryFrame(cap);
		if(!frame)
		{
			printf("CubieCam : Cannot load camera frame\n");
			break;
		}

		if(flag)
		{
			cvCvtColor(frame, grayImage0, CV_BGR2GRAY);
			cvAbsDiff(grayImage0, grayImage1, diffImage);
			cvThreshold(diffImage, diffImage, nThreshold, 255, CV_THRESH_BINARY);
			cvErode(diffImage, diffImage, NULL, 1);
			flag = 0;
		} else
		{
			cvCvtColor(frame, grayImage1, CV_BGR2GRAY);
			cvAbsDiff(grayImage1, grayImage0, diffImage);
			cvThreshold(diffImage, diffImage, nThreshold, 255, CV_THRESH_BINARY);
			cvErode(diffImage, diffImage, NULL, 1);
			flag = 1;
		}

		diffData = diffImage->imageData;
		for(i = 0; i < maxSize; i++)
		{
			if(diffData[i] != 0)
				cnt++;
		}

		rate = ((float)cnt / (float)maxSize) * 100;
		if(rate > 0.5)
		{
			printf("CubieCam : Ratio %5.2f\n", rate);
			if(d > 5)
			{
				t = time(NULL);
				tm = localtime(&t);
				tempStr = asctime(tm);
				tempStr[strlen(tempStr)-1] = 0;
				sprintf(filename, "./captures/%s.png", tempStr);
				cvSaveImage(filename, frame);
				printf("CubieCam : Capture Saved, Notificated\n");
				d = 0;
				if(port > 0)
				{
					sprintf(filename, "Captured/%s", tempStr);
					sendMessage(filename);
				}
			}
		}
	
		cnt = 0;
		if(d <= 5)
		{
			d++;
		}


		if(mode == STREAM_MODE)
		{
			cvReleaseCapture(&cap);
			pid_stream = fork();
			if(pid_stream == 0)
			{
				printf("CubieCam : Create mjpg-streamer process...\n");
				system("/home/cubie/mjpg-streamer/mjpg_streamer -i \"/home/cubie/mjpg-streamer/input_uvc.so -r 320x240 -f 15\" -o \"/home/cubie/mjpg-streamer/output_http.so -w /home/cubie/mjpg-streamer/www\"");
				return -1;
			} else if(pid_stream == -1)
			{
				printf("CubieCam : Cannot create mjpg-streamer process\n");
			} else
			{
				while(mode == STREAM_MODE){}
				kill(pid_stream, SIGTERM);
				kill(pid_stream+1, SIGTERM);
				kill(pid_stream+2, SIGTERM);
				kill(pid_stream+3, SIGTERM);
				waitpid(pid_stream, NULL, 0);
				waitpid(pid_stream+1, NULL, 0);
				waitpid(pid_stream+2, NULL, 0);
				waitpid(pid_stream+3, NULL, 0);
				printf("CubieCam : mjpg-streamer process %d stopped\n", pid_stream);
				sleep(2);
			}
			printf("CubieCam : Resume Monitoring...\n");
			cap = cvCaptureFromCAM(0);
		}


		if((c = cvWaitKey(10)) == 27)
		{
			printf("\nCubieCam : Quit capturing\n");
			break;
		}
	}

	cvReleaseImage(&grayImage1);
	cvReleaseImage(&grayImage0);
	cvReleaseImage(&diffImage);
	cvReleaseCapture(&cap);

	printf("CubieCam : Exit\n");
	return 0;
}

void *serialTask(void *params)
{
    int fd, i;
    struct termios oldtio, newtio;
    int readCnt;

    char buffer[BUFFER_LENGTH];
    char temp[BUFFER_LENGTH];

    fd = open(SERIAL_PORT,  O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd <0)
    {
    	printf("CubieCam/serialThread : Cannot Open %s\n", SERIAL_PORT);
        return 0;
    }

    tcgetattr(fd,&oldtio);
    memset(&newtio, 0x00, sizeof(newtio));
    configSerial(&newtio);
    tcflush(fd,TCIFLUSH);

    tcsetattr(fd,TCSANOW,&newtio);

    printf("CubieCam/serialTask : Serial Port Opened\n");

    while(1)
    {
    	readCnt = read(fd, buffer, BUFFER_LENGTH);
    	if(buffer[0] == STX)
    	{
			i = 1;
			while(buffer[i]!=ETX)
			{
				temp[i-1] = buffer[i];
				i++;
			}
    	}
    	if(port > 0 && readCnt > 10)
    	{
    		sendMessage(temp);
    	}
    	usleep(200000);
    }

    tcsetattr(fd,TCSANOW,&oldtio);
    close(fd);

    return 0;
}

void *serverTask(void *socket_desc)
{
	char buffer[BUFFER_LENGTH];
	char temp[20];
	struct sockaddr_in server_addr, client_addr;
	int server_fd, client_fd;
	int len, msg_size, i, m;

	if((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{
		printf("CubieCam/serverThread : Can't open stream socket\n");
		return 0;
	}

	memset(&server_addr, 0x00, sizeof(server_addr));

	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	server_addr.sin_port = htons(SERVER_PORT);

	while(bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <0)
	{
		printf("CubieCam/serverThread : Can't bind local address.\n");
		printf("CubieCam/serverThread : Rebind after 3 seconds\n");
		sleep(3);
		printf("CubieCam/serverThread : Rebinding...\n");
	}

	while(listen(server_fd, 5) < 0)
	{
		printf("CubieCam/serverThread : Can't listening connect.\n");
		printf("CubieCam/serverThread : Retry to listen connect");
	}

	memset(buffer, 0x00, sizeof(buffer));
	printf("CubieCam/serverThread : waiting connection request.\n");
	len = sizeof(client_addr);

	while(1)
	{
		client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&len);

		if(client_fd < 0)
		{
			printf("CubieCam/serverThread: accept failed.\n");
	        return 0;
		}

		inet_ntop(AF_INET, &client_addr.sin_addr.s_addr, temp, sizeof(temp));
		strcpy(serverIp, temp);
		printf("CubieCam/serverThread : %s client connected.\n", serverIp);

		msg_size = read(client_fd, buffer, BUFFER_LENGTH);
		if(buffer[0] == STX)
		{
			i = 1;
			while(buffer[i]!=ETX)
			{
				temp[i-1] = buffer[i];
				i++;
			}

			m = atoi(temp);

			if(m > 0)
			{
				port = m;
				printf("CubieCam/serverThread : set client port %d\n", port);
			}else
			{
				mode = m;
				printf("CubieCam/serverThread : set program mode %d\n", mode);
			}
		}
		close(client_fd);
		printf("CubieCam/serverThread : client closed.\n");
	}

	close(server_fd);
	return 0;
}

int sendMessage(char *str)
{
	int socket_fd, i, l, buffLen;
	struct sockaddr_in serv_addr;
	char buffer[30];

	buffLen = strlen(str) + 3;
	buffer[0] = STX;
	for(i=0;i<(buffLen-2);i++)
	{
		buffer[i+1] = str[i];
	}
	buffer[i] = ETX;

	memset(&serv_addr, 0x00, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = inet_addr(serverIp);
	serv_addr.sin_port = htons(port);

	printf("CubieCam : Send message to %s:%d\n", serverIp, port);

	socket_fd = socket(AF_INET, SOCK_STREAM, 0);
	if(socket_fd < 0)
	{
		printf("CubieCam : Cannot open client socket\n");
		return -1;
	}

	if(connect(socket_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0)
	{
		printf("CubieCam : Cannot connect to android app\n");
		if(mode == STREAM_MODE)
		{
			
			kill(pid_stream, SIGTERM);
			kill(pid_stream, SIGTERM);
			kill(pid_stream+1, SIGTERM);
			kill(pid_stream+2, SIGTERM);
			kill(pid_stream+3, SIGTERM);
			waitpid(pid_stream, NULL, 0);
			waitpid(pid_stream+1, NULL, 0);
			waitpid(pid_stream+2, NULL, 0);
			waitpid(pid_stream+3, NULL, 0);
			printf("CubieCam : mjpg-streamer process %d stopped\n", pid_stream);
			sleep(2);
			mode = MONITOR_MODE;
		}
	}

	if((l = write(socket_fd, buffer, buffLen)) < 0)
	{
		printf("CubieCam : Cannot send message to android app\n");
	}
	
	close(socket_fd);
	return 0;
}

int configSerial(struct termios *newtio)
{
    cfsetispeed(newtio, B115200);
    cfsetospeed(newtio, B115200);

    newtio->c_cflag &= ~PARENB;
    newtio->c_cflag &= ~CSTOPB;
    newtio->c_cflag &= ~CSIZE;
    newtio->c_cflag |= CS8;
    newtio->c_cflag &= ~CRTSCTS;
    newtio->c_cflag |= CLOCAL | CREAD;
    newtio->c_iflag &= ~INPCK;    /* Enable parity checking */
    newtio->c_iflag &= ~(ICRNL|IGNCR);
    newtio->c_iflag &= ~(IXON | IXOFF | IXANY);
    newtio->c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    newtio->c_cc[VMIN]=0;
    newtio->c_cc[VTIME]=20;
    return 0;
}
