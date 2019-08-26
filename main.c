#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <X11/Xlib.h>
#include <X11/X.h>
#include <pthread.h>
#include <sys/inotify.h>
#include <limits.h>
#include <sys/inotify.h>
#include <limits.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>


#define BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))


static pthread_mutex_t focusMutex;
static Window watchedWindow;
static Display *xdpy;
static const char *coordinateFilePath;
static int imeX = 0;
static int imeY = 0;
static int inotifyFd = -1;
static int inotifyWd = -1;

static void safeExit(int exitCode) {
	if (inotifyFd != -1 && inotifyWd != -1) {
		inotify_rm_watch(inotifyFd, inotifyWd);
	}
	exit(exitCode);
}

static void* watchFocusWindow(void *void_ptr) {
	XInitThreads();
 	Window focusWindow;
 	int unused_revert_ret;
 	while (1) {
		if (1 != XGetInputFocus(xdpy, &focusWindow, &unused_revert_ret)) {
			fprintf(stderr, "Error: Unable to get focus window\n");
			safeExit(1);
		}
		if (focusWindow != watchedWindow) {
			fprintf(stderr, "Error: Window lost focus\n");
			safeExit(1);
		}
		sleep(1);
    }
    pthread_exit(0);
    return NULL;
}

static void readCoordinateFile() {
	int x, y;
	char buf[256] = {0};
	FILE *file = fopen(coordinateFilePath, "r");
	if (file == NULL) {
		fprintf(stderr, "Error: Unable to open file, %s\n", coordinateFilePath);
		return;
	}
	int unused = fread(buf, 1, sizeof(buf) - 1, file);
	printf("CoordinateFile content: %s\n", buf);
	if (2 == sscanf(buf, "x=%d\ny=%d", &x, &y)) {
		printf("CoordinateFile x:%d y:%d\n", x, y);
		imeX = x;
		imeY = y;
	} else {
		fprintf(stderr, "Error: CoordinateFile format incorrect\n");
	}
	fclose(file);
}

static void* watchCoordinateFilePoll(void *void_ptr) {

	struct stat fileStat;
	time_t lastModifyTime = 0;

	while (1) {
		 if (stat(coordinateFilePath, &fileStat) < 0) {
		 	fprintf(stderr, "Error: Stat file %s fail\n", coordinateFilePath);
		 	sleep(1);
		 	continue;
		 }
	 	 if (fileStat.st_mtime > lastModifyTime) {
		 	printf("fileStat.st_mtime=%lu lastModifyTime=%lu\n", fileStat.st_mtime, lastModifyTime);
		 	lastModifyTime = fileStat.st_mtime;
		 	readCoordinateFile();
		 }
		 usleep(20 * 1000);
	}
	
    pthread_exit(0);
    return NULL;
}

static void* watchCoordinateFileInotify(void *void_ptr) {
	ssize_t numRead;
	char buf[BUF_LEN] __attribute__ ((aligned(8)));
	struct inotify_event *event;
	char *p;
    inotifyFd = inotify_init();
    if (inotifyFd == -1) {
		fprintf(stderr, "Error: Init inotify fail, switch to poll mechanism\n");
		return watchCoordinateFilePoll(void_ptr);
    }
    int inotifyWd = inotify_add_watch(inotifyFd, coordinateFilePath, IN_MODIFY);
    if (inotifyWd == -1) {
		fprintf(stderr, "Error: Add inotify watch fail, %s, switch to poll mechanism.\n", strerror(errno));
		return watchCoordinateFilePoll(void_ptr);
    }
    while (1) {                            
         numRead = read(inotifyFd, buf, BUF_LEN);
         if (numRead == 0) {
			fprintf(stderr, "Error: read() from inotify fd returned 0!\n");
			safeExit(1);
         }
 
         if (numRead == -1) {
			fprintf(stderr, "Error: Inotify read() fail\n");
			safeExit(1);
         }
 
         printf("Read %ld bytes from inotify fd\n", (long) numRead);
 
         /* Process all of the events in buffer returned by read() */
 
         for (p = buf; p < buf + numRead; ) {
             event = (struct inotify_event *) p;
             if (event->mask & IN_MODIFY) {
         	     printf("IN_MODIFY\n");
         	     readCoordinateFile();
             }  
             p += sizeof(struct inotify_event) + event->len;
         }
     }
    pthread_exit(0);
    return NULL;
}

int main(int argc, char *argv[]) {
	if (argc != 4) {
		fprintf(stderr, "Usage: %s inputWindow watchedWindow coordinateFilePath\n", argv[0]);
		return 1;
	}
	if (argv[1] == NULL) {
		fprintf(stderr, "Usage: Invaild param inputWindow\n");
		return 1;
	}
	if (argv[2] == NULL) {
		fprintf(stderr, "Usage: Invaild param watchedWindow\n");
		return 1;
	}
	if (argv[3] == NULL) {
		fprintf(stderr, "Usage: Invaild param coordinateFilePath\n");
		return 1;
	}
	printf("inputWindow: %s\n", argv[1]);
	printf("watchedWindow: %s\n", argv[2]);
	printf("coordinateFilePath: %s\n", argv[3]);
	Window inputWindow = (Window)strtol(argv[1], NULL, 0);
	watchedWindow = (Window)strtol(argv[2], NULL, 0);
	coordinateFilePath = argv[3];
 
	if (access(coordinateFilePath, F_OK) == -1) {
		fprintf(stderr, "Error: File %s not exists\n", coordinateFilePath);
		return 1;
	}

	XInitThreads();
	const char *displayName = getenv("DISPLAY");
	if ((xdpy = XOpenDisplay(displayName)) == NULL) {
		/* Can't use _xdo_eprintf yet ... */
		fprintf(stderr, "Error: Can't open display: %s\n", displayName);
		return 1;
	}

	pthread_t t;
	if (pthread_create(&t, NULL, &watchFocusWindow, NULL) == -1) {
		fprintf(stderr, "Error: Unable to create watch focus window thread\n");
        return 1;
    }

	if (pthread_create(&t, NULL, &watchCoordinateFileInotify, NULL) == -1) {
		fprintf(stderr, "Error: Unable to create watch coordinate file thread\n");
        return 1;
    }

	XWindowAttributes attr;
	XEvent event;
  
  	while (1) {  		
  		if (imeX == 0 && imeY == 0) {
  			readCoordinateFile();
  			usleep(200 * 1000);
  			continue;
  		}
  		if (0 != XGetWindowAttributes(xdpy, inputWindow, &attr)) {
  			if (attr.x == imeX && attr.y == imeY) {
  				usleep(100);
  				continue;
  			}
  			if (attr.map_state != IsViewable) {
  				usleep(2 * 1000);
  				continue;
  			} 
  		}
  		XMoveWindow(xdpy, inputWindow, imeX + 10 , imeY + 20);
	  	//XConfigureWindow(xdpy, window, CWX | CWY, &wc);
		usleep(100);
	  	//sleep(0);
  	}

	return 0;
}