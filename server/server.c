#include "server.h"

void* routineProducer(void* arg) {
    usleep(2000); 

    Message* params = arg; // Casts arg from void* to Message*
    params->tskres = (timeOut) ? TSKTIMEOUT : task(params->tskload);

    Message serverMessage = *params; // Copy of params used to preserve original message 
    parseMessage(&serverMessage); // Initializes the various attributes of this Message instance
    registOperation(serverMessage, "TSKEX");

    sem_wait(&semBufferEmpty); // Used to decrement the number of available slots at the buffer, waits if none is available
    pthread_mutex_lock(&bufferMutex);

    buffer[producerIndex] = *params; // Writes to buffer each request's message
    producerIndex++;
    producerIndex %= bufsz; // Reutilizes previously used buffer indexes and prevents segmentation fault

    pthread_mutex_unlock(&bufferMutex);
    sem_post(&semBufferFull);  // Used to increment the number of available slots at the buffer

    free(params);
    pthread_exit(NULL);
}   

void* routineConsumer(void* arg) {
    
    Message message;
    char privateFifo[MAX_BUF];
    int privateID;
    while (1) {
        if (timeOut && (unreadMessages == 0)) break; // If server timed out and all messages on buffer were already read, break loop

        if (sem_trywait(&semBufferFull) != 0) // Used to decrement the number of messages to be read
            continue; // If there's no message to be read, iterates while loop again

        pthread_mutex_lock(&bufferMutex); 

        message = buffer[consumerIndex];  
        consumerIndex++;
        consumerIndex %= bufsz;

        pthread_mutex_unlock(&bufferMutex);
        sem_post(&semBufferEmpty);
        
        snprintf(privateFifo, MAX_BUF, "/tmp/%d.%lu", message.pid, message.tid); // Names a FIFO based on the process id and thread id
        
	    parseMessage(&message);
        
	    if ((privateID = open(privateFifo, O_WRONLY)) < 0)
            registOperation(message, "FAILD"); // If fifo failed to open, send FAILD
        else{

            if (write(privateID, &message, sizeof(Message)) < 0) { 
                registOperation(message, "FAILD"); // If write failed, send FAILD
            } else {
                // Sends 2LATE if server timed out
                if (message.tskres == TSKTIMEOUT) registOperation(message, "2LATE");
                else registOperation(message, "TSKDN"); // If no problem occured, send TSKDN
            }        
            close(privateID);
        }

        pthread_mutex_lock(&unreadMessagesMutex);
        unreadMessages--;
        pthread_mutex_unlock(&unreadMessagesMutex);
        
    }
    pthread_exit(NULL);
}

int requestReceiver(int t, int publicFD) {
    int nthreads = 0; // Thread counter
    Message message;
    pthread_t thread; // Declares a pthread_t variable, where a producer thread will be created at every while loop iteration
    pthread_t consumerThread; // Declares a pthread_t variable, created once before main loop

    pthreadLinked* start = NULL;    // First pthread at linked list. Is used as a pseudo constant variable, since the first element rarely changes
    pthreadLinked* current = NULL;  // Current pthread at linked list


    time_t initT = time(NULL);      // Stores the while's first iteration's exact time 
    time_t nowT;                    // Will store each while iteration's exact time


    buffer = malloc(sizeof(Message) * bufsz);


    if (pthread_create(&consumerThread, NULL, routineConsumer, NULL) != 0) {  
        free(buffer);
        perror("Error creating new thread");
        return 1;
    }

    while (1) { 
        if (time(&nowT) - initT >= t) { // Verifies if time elapsed since first iteration exceeds maximum time
            close(publicFD);
            timeOut = 1;
            break;
        }

        if (read(publicFD, &message, sizeof(Message)) == sizeof(Message)) {
            nthreads++;
            Message* args = malloc(sizeof(*args)); // Will store each thread's arguments

            *args = message;

            registOperation(message, "RECVD");

            // If thread creation fails, waits 1000 microseconds and tries again.
            while (pthread_create(&thread, NULL, routineProducer, args) != 0) usleep(1000);

            pthread_mutex_lock(&unreadMessagesMutex);
            unreadMessages++;
            pthread_mutex_unlock(&unreadMessagesMutex);
            

            if (nthreads == 1) { // If it is the first thread, startLinkedList is called and the linked list is initialized
                startLinkedList(thread, &start); // Initializes the first element of the linked list, which will be pointed to by "start" pointer
                current = start; // At this stage, the starting element is the current element. This will be changed with further insertions.
            } else {
                insertThread(thread, &current); // Adds new element to the linked list, which will be pointed to by the "current" pointer
            }

        }

    }


    close(publicFD); // Ceases communication between server and client

    current = start; // Starting at the first thread

    while (current != NULL) { // Until the thread pointed to by the last thread is null
        if (pthread_join(current->thread, NULL) != 0) { // Joins ProducerThreads
            free(buffer);
            freeLinkedList(&start);
            perror("Error joining threads");
            return 1;
        }
        current = current->next; // Jumps to next thread
    }
    

    if (pthread_join(consumerThread, NULL) != 0) { //Joins Consumer thread
        free(buffer);
        freeLinkedList(&start);
        perror("Error joining threads");
        return 1;
    }

    free(buffer);
    freeLinkedList(&start);
    return 0;
}


int main(int argc, char* argv[]) {
    char* publicFIFO;
    int nsecs, publicFD;

    pthread_mutex_init(&bufferMutex, NULL);
    pthread_mutex_init(&unreadMessagesMutex, NULL);
    
    producerIndex = 0;
    consumerIndex = 0;

    timeOut = 0;
    unreadMessages = 0;
    
    switch(argc) {
        case UNDEF_BUFSZ: // If -l flag not present in commmand line arguments
           bufsz = DEFAULT_BUFSZ;   
           publicFIFO = argv[3];
           break;

        case DEF_BUFSZ: // If -l flag present in command line arguments
            bufsz = (atoi(argv[4]) > 0) ? atoi(argv[4]) : DEFAULT_BUFSZ;
            publicFIFO = argv[5];
            break;

        default: 
            perror("Error at number of arguments");
            return 1;
    }

    sem_init(&semBufferEmpty, 0, bufsz);
    sem_init(&semBufferFull, 0, 0);
        
    nsecs = atoi(argv[2]);

    if (mkfifo(publicFIFO, PERM) == -1) { // Creates FIFO for communication between this thread and the server 
        perror("Error creating private FIFO");
        return 1;
    }

    if ((publicFD = open(publicFIFO, O_RDONLY | O_NONBLOCK, PERM)) == -1) {
        perror("Error opening public file descriptor");
        return 1;
    }

    if (requestReceiver(nsecs, publicFD) == 1) { // Invokes requestReceiver and handles errors 
        perror("Error in requestReceiver\n");
    }
    
    remove(publicFIFO);
    
    sem_destroy(&semBufferEmpty);
    sem_destroy(&semBufferFull);
    
    pthread_mutex_destroy(&bufferMutex);
    pthread_mutex_destroy(&unreadMessagesMutex);


    return 0;
}