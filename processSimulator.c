#include <stdio.h>
#include <stdlib.h>
#include "linkedlist.h"
#include <sys/time.h>
#include "coursework.h"
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>

struct thread_parameter{
    struct process *process_table[SIZE_OF_PROCESS_TABLE];
    struct element *free_pid_head;
    struct element *free_pid_tail;
    struct element *newqueue_head;
    struct element *newqueue_tail;
    struct element *readyqueue_head[MAX_PRIORITY];
    struct element *readyqueue_tail[MAX_PRIORITY];
    struct element *terminatequeue_head;
    struct element *terminatequeue_tail;
    struct process *running[NUMBER_OF_CPUS];
    int *counter;   // count for the number of process finished in terminated queue
};

struct consumer_parameter{
    struct thread_parameter *thr;
    int consumer;
    int *process_counter;    // count for the number of process finished in ready queue
};

struct timeval oBaseTime;

pthread_mutex_t mutex_table = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t mutex_pid = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t mutex_running = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t mutex_new = PTHREAD_MUTEX_INITIALIZER; 
pthread_mutex_t mutex_ready = PTHREAD_MUTEX_INITIALIZER;  
pthread_mutex_t mutex_terminate = PTHREAD_MUTEX_INITIALIZER;

sem_t ready, spid;  // ready queue and free pid

void *process_generator(void *p);   
void *longterm_scheduler(void *p);   
void *shortterm_scheduler(void *p);   
void *termination(void *p);    
void *booster(void *p);    

void printHeadersSVG();
void printPrioritiesSVG();
void printRasterSVG();
void printProcessSVG(int iCPUId, struct process * pProcess, struct timeval oStartTime, struct timeval oEndTime);
void printFootersSVG();

int main(void){
    // initializations
    gettimeofday(&oBaseTime, NULL);
    sem_init(&ready, 0, 0);
    sem_init(&spid, 0, 0);  
    
    struct thread_parameter para;
    para.free_pid_head = NULL;
    para.free_pid_tail = NULL;
    para.newqueue_head = NULL;
    para.newqueue_tail = NULL;
    para.terminatequeue_head = NULL;
    para.terminatequeue_tail = NULL;
    // initialise process counters
    int counter1 = 0;
    para.counter = &counter1;
    // initialise free pid list
    int pid[SIZE_OF_PROCESS_TABLE];
    for (int i = 0; i < SIZE_OF_PROCESS_TABLE; i++){
        pid[i] = i;
        addLast(&pid[i], &(para.free_pid_head), &(para.free_pid_tail));
        sem_post(&spid);
    }
    // initialise ready queue
    for (int i = 0; i < MAX_PRIORITY; i++){
        para.readyqueue_head[i] = NULL;
        para.readyqueue_tail[i] = NULL;
    }
    // initialise running process
    for (int i = 0; i < NUMBER_OF_CPUS; i++){
        para.running[i] = NULL;
    }
    // initialise CPU
    struct consumer_parameter con_para[NUMBER_OF_CPUS];
    int counter2 = 0;
    for (int i =  0; i < NUMBER_OF_CPUS; i++){
        con_para[i].thr = &para;
        con_para[i].consumer = i + 1;
        con_para[i].process_counter = &counter2;
    }

    printHeadersSVG();
    printPrioritiesSVG();
    printRasterSVG();
   
    // create threads 
    void *retval;
    pthread_t generate_process, longterm, terminate, priorboost;
    // process generator, longterm scheduler
    int thread1 = pthread_create(&generate_process, NULL, process_generator, &para);
    int thread2 = pthread_create(&longterm, NULL, longterm_scheduler, &para);
    // shortterm scheduler
    int thread[NUMBER_OF_CPUS];
    pthread_t tid[NUMBER_OF_CPUS];
    for (int i =  0; i < NUMBER_OF_CPUS; i++){    
        thread[i] = pthread_create(&tid[i], NULL, shortterm_scheduler, &con_para[i]);
    }
    // termination, priority booster
    int thread5 = pthread_create(&terminate, NULL, termination, &para);
    int thread6 = pthread_create(&priorboost, NULL, booster, &para);
    // make sure the main thread end after child thread
    int temp1 = pthread_join(generate_process, &retval);
    if (temp1 != 0){
        printf("join failed.\n");
    }
    int temp2 = pthread_join(longterm, &retval);
    if (temp2 != 0){
        printf("join failed.\n");
    }

    int temp[NUMBER_OF_CPUS];
    for (int i =  0; i < NUMBER_OF_CPUS; i++){
         temp[i] = pthread_join(tid[i], &retval);
    }
    int temp5 = pthread_join(terminate, &retval);
    if (temp5 != 0){
        printf("join failed.\n");
    }
    int temp6 = pthread_join(priorboost, &retval);
    if (temp6 != 0){
        printf("join failed.\n");
    }

    printFootersSVG();
}

void *process_generator(void *p){
    int counter = 0;    // count for the number of generated job
    struct thread_parameter *para = p;    
    while (counter < NUMBER_OF_PROCESSES){        
        sem_wait(&spid);    // check free pid      
        pthread_mutex_lock(&mutex_pid);    // use a free pid, generate a process at process table  
        int *pid = removeFirst(&(para->free_pid_head), &(para->free_pid_tail));
        pthread_mutex_unlock(&mutex_pid);
        pthread_mutex_lock(&mutex_table);
        (para->process_table[*pid]) = generateProcess(pid);
        pthread_mutex_unlock(&mutex_table); 
        struct process *p1 = para->process_table[*pid];       
        pthread_mutex_lock(&mutex_new);    // add it to new queue
        addLast(p1, &(para->newqueue_head), &(para->newqueue_tail));
        printf("TXT: Generated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *pid, p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime, getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning), getDifferenceInMilliSeconds(p1->oFirstTimeRunning, p1->oLastTimeRunning)); 
        pthread_mutex_unlock(&mutex_new);         
        counter++;                                             
    }
}

void *longterm_scheduler(void *p){
    int counter = 0;    // count for the number of jobs into ready queue
    struct thread_parameter *para = p;  
    while (counter < NUMBER_OF_PROCESSES){       
        while (para->newqueue_head != NULL){
            pthread_mutex_lock(&mutex_new);    // remove a job in new queue
            struct process *p1 = removeFirst(&(para->newqueue_head), &(para->newqueue_tail));
            pthread_mutex_unlock(&mutex_new);
            int priority = p1->iPriority;
            pthread_mutex_lock(&mutex_ready);    // add the job to ready queue
            addLast(p1,&(para->readyqueue_head[priority]), &(para->readyqueue_tail[priority]));
            printf("TXT: Admitted: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);  
            pthread_mutex_unlock(&mutex_ready);
            // find the lowest FCFS running job
            int minpriority = -1;
            pthread_mutex_lock(&mutex_running); 
            for (int i = 0; i < NUMBER_OF_CPUS; i++){
                if (para->running[i] != NULL && priority < (para->running[i])->iPriority){
                    if (minpriority == -1){
                        minpriority = i;
                    }
                    else {
                        if ((para->running[minpriority])->iPriority > (para->running[i])->iPriority){
                            minpriority = i;
                        }
                    }
                }
            }
            pthread_mutex_unlock(&mutex_running); 
            // if new job has higher priority, then preempt
            if (minpriority != -1){
                preemptJob(para->running[minpriority]);
            }
            counter++;                   
            sem_post(&ready);  
        }        
        usleep(LONG_TERM_SCHEDULER_INTERVAL);
    }
}


void *shortterm_scheduler(void *p){   
    struct consumer_parameter *con_para = p;
    struct thread_parameter *para = con_para->thr;
    int consumer = con_para->consumer;
    int *counter = con_para->process_counter;
    struct timeval start, end;

    while ((*counter) < NUMBER_OF_PROCESSES){
        sem_wait(&ready);    // check if ready queue is empty
        pthread_mutex_lock(&mutex_ready);
        // find the highest job and run
        for (int i = 0; i < MAX_PRIORITY; i++){ 
            // run as FCFS           
            if (i < MAX_PRIORITY/2){
                // if there is a job, run; else, continue to find job in next priority           
                if (para->readyqueue_head[i] != NULL){                   
                    struct process *p1 = removeFirst(&(para->readyqueue_head[i]), &(para->readyqueue_tail[i]));    // remove from ready queue               
                    pthread_mutex_unlock(&mutex_ready);
                    pthread_mutex_lock(&mutex_running);
                    para->running[consumer] = p1;    // set it to running status
                    pthread_mutex_unlock(&mutex_running);
                    runNonPreemptiveJob(p1, &start, &end);    // run the job
                    pthread_mutex_lock(&mutex_running);
                    para->running[consumer] = NULL;    // reset running status
                    pthread_mutex_unlock(&mutex_running);
                    // if process finished, move to terminated queue; else, back to ready queue
                    if (p1->iRemainingBurstTime == 0){
                        pthread_mutex_lock(&mutex_terminate);
                        addLast(p1,&(para->terminatequeue_head), &(para->terminatequeue_tail));                       
                        printf("TXT: Consumer%d: Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, ", consumer, *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);
                        // if the job finished within one run, print respond time and turnaround time; else print turnaround time
                        if (p1->iPreviousBurstTime == p1->iInitialBurstTime){
                            printf("Response Time = %d, Turnaround Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning), getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oLastTimeRunning));
                        }
                        else {
                            printf("Turnaround Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oLastTimeRunning));
                        }
                        printProcessSVG(consumer, p1, start, end);
                        pthread_mutex_unlock(&mutex_terminate);
                        (*counter)++;                  
                    }
                    else{
                        pthread_mutex_lock(&mutex_ready);
                        addLast(p1,&(para->readyqueue_head[i]), &(para->readyqueue_tail[i]));                        
                        printf("TXT: Consumer%d: Process Id = %d (FCFS), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d", consumer, *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);
                        // if job runs the first time, print respond time
                        if (p1->iPreviousBurstTime == p1->iInitialBurstTime){
                            printf(", Response Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning));
                        }
                        else{
                            printf("\n");
                        }
                        printProcessSVG(consumer, p1, start, end);
                        pthread_mutex_unlock(&mutex_ready);
                        sem_post(&ready);
                    }
                    break;
                }
                else{
                    continue;
                }
            }
            // run RR
            else{
                // if there is a job, run; else, continue to find job in next priority  
                if (para->readyqueue_head[i] != NULL){
                    struct process *p1 = removeFirst(&(para->readyqueue_head[i]), &(para->readyqueue_tail[i]));    // remove from ready queue 
                    pthread_mutex_unlock(&mutex_ready);
                    runPreemptiveJob(p1, &start, &end);    // run the job
                    // if process finished, move to terminated queue; else, back to ready queue
                    if (p1->iRemainingBurstTime == 0){
                        pthread_mutex_lock(&mutex_terminate);
                        addLast(p1,&(para->terminatequeue_head), &(para->terminatequeue_tail));                       
                        printf("TXT: Consumer%d: Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, ", consumer, *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);
                        if (p1->iPreviousBurstTime == p1->iInitialBurstTime){
                            printf("Response Time = %d, Turnaround Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning), getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oLastTimeRunning));
                        }
                        else {
                            printf("Turnaround Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oLastTimeRunning));
                        }
                        printProcessSVG(consumer, p1, start, end);
                        pthread_mutex_unlock(&mutex_terminate);
                        (*counter)++;
                        
                    }
                    else{
                        pthread_mutex_lock(&mutex_ready);
                        addLast(p1,&(para->readyqueue_head[i]), &(para->readyqueue_tail[i]));                       
                        printf("TXT: Consumer%d: Process Id = %d (RR), Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d", consumer, *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);
                        if (p1->iPreviousBurstTime == p1->iInitialBurstTime){
                            printf(", Response Time = %d\n", getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning));
                        }
                        else {
                            printf("\n");
                        }
                        printProcessSVG(consumer, p1, start, end);
                        pthread_mutex_unlock(&mutex_ready);
                        sem_post(&ready);
                    }                   
                    break;
                }
                else{
                    continue;
                }
                
            }         
        }                             
    }
    // used to end all shortterm scheduler, when all the jobs are finished
    sem_post(&ready);
    pthread_mutex_unlock(&mutex_ready);
}


void *termination(void *p){   
    struct thread_parameter *para = p;
    int *counter = para->counter;
    int total_respond = 0, total_turnaround = 0;
    while ((*counter) < NUMBER_OF_PROCESSES){ 
        // remove the job from terminate queue, add free pid and process table, do calculation
        while (para->terminatequeue_head != NULL){
            pthread_mutex_lock(&mutex_terminate);
            struct process *p1 = removeFirst(&(para->terminatequeue_head), &(para->terminatequeue_tail));           
            int *pid = p1->pPID;
            pthread_mutex_lock(&mutex_table);
            para->process_table[*pid] = NULL;
            pthread_mutex_unlock(&mutex_table);
            pthread_mutex_lock(&mutex_pid);
            addLast(pid, &(para->free_pid_head), &(para->free_pid_tail));
            pthread_mutex_unlock(&mutex_pid);
            int respond = getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oFirstTimeRunning);
            int turnaround = getDifferenceInMilliSeconds(p1->oTimeCreated, p1->oLastTimeRunning);
            total_respond += respond;
            total_turnaround += turnaround;
            printf("TXT: Terminated: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d, Response Time = %d, Turnaround Time = %d\n", *pid, p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime, respond, turnaround);
            pthread_mutex_unlock(&mutex_terminate);
            (*counter)++;          
            sem_post(&spid);
        }        
        usleep(TERMINATION_INTERVAL);
    }
    printf("TXT: Average respond time: %f, average turnaround time: %f\n", (float)total_respond/NUMBER_OF_PROCESSES, (float)total_turnaround/NUMBER_OF_PROCESSES); 
}

void *booster(void *p){
    struct thread_parameter *para = p;
    int *counter = para->counter;
    while((*counter) < NUMBER_OF_PROCESSES){
        // boost the first job in the 17-31 ready queue to the end of 16 ready queue
        usleep(BOOST_INTERVAL);
        pthread_mutex_lock(&mutex_ready);
        for (int i = MAX_PRIORITY/2 + 1; i < MAX_PRIORITY; i++){            
            if (para->readyqueue_head[i] != NULL){
                struct process *p1 = removeFirst(&(para->readyqueue_head[i]), &(para->readyqueue_tail[i]));

                addLast(p1,&(para->readyqueue_head[MAX_PRIORITY/2]), &(para->readyqueue_tail[MAX_PRIORITY/2]));
                printf("TXT: Boost: Process Id = %d, Priority = %d, Previous Burst Time = %d, Remaining Burst Time = %d\n", *(p1->pPID), p1->iPriority, p1->iPreviousBurstTime, p1->iRemainingBurstTime);
            }
        }
        pthread_mutex_unlock(&mutex_ready);
    }
}

void printHeadersSVG()
{
    printf("SVG: <!DOCTYPE html>\n");
    printf("SVG: <html>\n");
    printf("SVG: <body>\n");
    printf("SVG: <svg width=\"15000\" height=\"4000\">\n");
}


void printProcessSVG(int iCPUId, struct process * pProcess, struct timeval oStartTime, struct timeval oEndTime)
{
    int iXOffset = getDifferenceInMilliSeconds(oBaseTime, oStartTime) + 30;
    int iYOffsetPriority = (pProcess->iPriority + 1) * 16 - 12;
    int iYOffsetCPU = (iCPUId - 1 ) * (480 + 50);
    int iWidth = getDifferenceInMilliSeconds(oStartTime, oEndTime);
    printf("SVG: <rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"8\" style=\"fill:rgb(%d,0,%d);stroke-width:1;stroke:rgb(255,255,255)\"/>\n", iXOffset /* x */, iYOffsetCPU + iYOffsetPriority /* y */, iWidth, *(pProcess->pPID) - 1 /* rgb */, *(pProcess->pPID) - 1 /* rgb */);
}

void printPrioritiesSVG()
{
    for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
    {
	for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
	{
	    int iYOffsetPriority = (iPriority + 1) * 16 - 4;
	    int iYOffsetCPU = (iCPU - 1) * (480 + 50);
	    printf("SVG: <text x=\"0\" y=\"%d\" fill=\"black\">%d</text>", iYOffsetCPU + iYOffsetPriority, iPriority);
	}
    }
}
void printRasterSVG()
{
    for(int iCPU = 1; iCPU <= NUMBER_OF_CPUS;iCPU++)
    {
	for(int iPriority = 0; iPriority < MAX_PRIORITY; iPriority++)
	{	
            int iYOffsetPriority = (iPriority + 1) * 16 - 8;
	    int iYOffsetCPU = (iCPU - 1) * (480 + 50);
    	    printf("SVG: <line x1=\"%d\" y1=\"%d\" x2=\"15000\" y2=\"%d\" style=\"stroke:rgb(125,125,125);stroke-width:1\" />", 16, iYOffsetCPU + iYOffsetPriority, iYOffsetCPU + iYOffsetPriority);
	}
    }
}

void printFootersSVG()
{
    printf("SVG: Sorry, your browser does not support inline SVG.\n");
    printf("SVG: </svg>\n");
    printf("SVG: </body>\n");
    printf("SVG: </html>\n");
}




