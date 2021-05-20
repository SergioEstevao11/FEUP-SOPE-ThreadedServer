#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>
#include <pthread.h>
#include <signal.h>

#include "common.h"
#include "lib.h"

void registOperation(Message* message, const char* oper);

void parseMessage(Message* message);