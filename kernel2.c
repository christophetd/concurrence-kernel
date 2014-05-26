#include <stdio.h>
#include <stdlib.h>
#include "system_m.h"
#include "interrupt.h"
#include "kernel2.h"

/************* Symbolic constants and macros ************/
#define MAX_PROC 10
#define MAX_MONITORS 10

#define DPRINTA(text, ...) printf("[%d] " text "\n", head(&readyList), __VA_ARGS__)
#define DPRINT(text) DPRINTA(text, 0)
#define ERRA(text, ...) fprintf(stderr, "[%d] Error: " text "\n", head(&readyList), __VA_ARGS__)
#define ERR(text) { ERRA(text, 0); int i; for(i = 0; i < 75000; ++i){} }

/************* Data structures **************/
typedef struct {
	int next;
	Process p;
	int currentMonitor;			/* points to the monitors array */
	int monitors[MAX_MONITORS + 1]; /* used for nested calls; monitors[0] is always -1 */
	int timeout;
} ProcessDescriptor;

typedef struct {
	int timesTaken;
	int takenBy;
	int entryList;
	int waitingList;
} MonitorDescriptor;

/********************** Global variables **********************/

/* Pointer to the head of the ready list */
static int readyList = -1;

/* List of process descriptors */
ProcessDescriptor processes[MAX_PROC];
static int nextProcessId = 0;

/* List of monitor descriptors */
MonitorDescriptor monitors[MAX_MONITORS];
static int nextMonitorId = 0;

/* Part 2 of the project variables and data structures */
#define STACK_SIZE	10000
#define TIME_SLICING_FREQUENCY	20 // ms
#define CLOCK_PERIOD 1 // ms

int idle_pid;
int scheduler_pid;
int timedWaiting[MAX_PROC] = {0};


/*************** Functions for process list manipulation **********/

/* add element to the tail of the list */
static void addLast(int* list, int processId) {
	if (*list == -1){
		*list = processId;
	}
	else {
		int temp = *list;
		while (processes[temp].next != -1){
			temp = processes[temp].next;
		}
		processes[temp].next = processId;
	}
	processes[processId].next = -1;
}

/* add element to the head of list */
static void addFirst(int* list, int processId){
	processes[processId].next = *list;
	*list = processId;
}

int size(int* list) {
	int i;
	for (i=0 ; *list != -1 ; i++) {
		list = &processes[*list].next;
	}

	return i;
}

/* remove an element from the head of the list */
static int removeHead(int* list){
	if (*list == -1){
		return(-1);
	}
	else {
		int head = *list;
		int next = processes[*list].next;
		processes[*list].next = -1;
		*list = next;
		return head;
	}
}

static void removeFromList(int* list, int processId) {
	int i;

	for (i=0 ; *list != -1 ; i++) {
		if (*list == processId) {
			*list = processes[processId].next;
			processes[processId].next = -1;
			break;
		}
		list = &processes[*list].next;
	}
}

/* returns the head of the list */
static int head(int* list){
	return *list;
}

/* checks if the list is empty */
static int isEmpty(int* list) {
	return *list < 0;
}

/***********************************************************
 ***********************************************************
                    Kernel functions
************************************************************
* **********************************************************/

void createProcess (void (*f)(), int stackSize) {
	if (nextProcessId == MAX_PROC){
		ERR("Maximum number of processes reached!");
		exit(1);
	}
	unsigned int* stack = malloc(stackSize);
	if (stack==NULL) {
		ERR("Could not allocate stack. Exiting...");
		exit(1);
	}
	processes[nextProcessId].p = newProcess(f, stack, stackSize);
	processes[nextProcessId].next = -1;
	processes[nextProcessId].currentMonitor = 0;
	processes[nextProcessId].monitors[0] = -1;
	processes[nextProcessId].timeout = -1;

	addLast(&readyList, nextProcessId);
	nextProcessId++;
}

int createSpecialProcess(void (*f)()) {

	if (nextProcessId == MAX_PROC){
		ERR("Maximum number of processes reached!");
		exit(1);
	}

	unsigned int* stack = malloc(STACK_SIZE);
	if (stack==NULL) {
		ERR("Could not allocate stack. Exiting...");
		exit(1);
	}
	processes[nextProcessId].p = newProcess(f, stack, STACK_SIZE);
	processes[nextProcessId].next = -1;
	processes[nextProcessId].currentMonitor = 0;
	processes[nextProcessId].monitors[0] = -1;
	processes[nextProcessId].timeout = -1;

	int pid = nextProcessId;
	nextProcessId++;
	return pid;
}

static void checkAndTransfer() {
	/*if (isEmpty(&readyList)){
		/*ERR("No processes in the ready list! Exiting...");
		exit(1);
	}*/
	int pid = isEmpty(&readyList) ? idle_pid : head(&readyList);
	transfer(processes[pid].p);
}



void yield(){
	maskInterrupts();
	int pid = isEmpty(&readyList) ? idle_pid : removeHead(&readyList);
	addLast(&readyList, pid);
	checkAndTransfer();
	allowInterrupts();
}

int createMonitor(){
	maskInterrupts();
	if (nextMonitorId == MAX_MONITORS){
		ERR("Maximum number of monitors reached!\n");
		exit(1);
	}
	monitors[nextMonitorId].timesTaken = 0;
	monitors[nextMonitorId].takenBy = -1;
	monitors[nextMonitorId].entryList = -1;
	monitors[nextMonitorId].waitingList = -1;
	int mid = nextMonitorId;
	nextMonitorId++;
	allowInterrupts();
	return mid;
}

static int getCurrentMonitor(int pid) {
	int result = processes[pid].monitors[processes[pid].currentMonitor];
	return result;
}

void enterMonitor(int monitorID) {
	maskInterrupts();

	int myID = head(&readyList);

	if (monitorID > nextMonitorId || monitorID < 0) {
		ERRA("Monitor %d does not exist.", nextMonitorId);
		exit(1);
	}

	if (processes[myID].currentMonitor >= MAX_MONITORS) {
		ERR("Too many nested calls.");
		exit(1);
	}

	if (monitors[monitorID].timesTaken > 0 && monitors[monitorID].takenBy != myID) {
		removeHead(&readyList);
		addLast(&(monitors[monitorID].entryList), myID);
		checkAndTransfer();

		/* I am woken up by exitMonitor -- check if the monitor state is consistent */
		if ((monitors[monitorID].timesTaken != 1) || (monitors[monitorID].takenBy != myID)) {
			ERR("The kernel has performed an illegal operation. Please contact customer support.");
			exit(1);
		}
	}
	else {
		monitors[monitorID].timesTaken++;
		monitors[monitorID].takenBy = myID;
	}

	/* push the new call onto the call stack */
	processes[myID].monitors[++processes[myID].currentMonitor] = monitorID;

	allowInterrupts();
}

void exitMonitor() {
	maskInterrupts();

	int myID = head(&readyList);
	int myMonitor = getCurrentMonitor(myID);

	if (myMonitor < 0) {
		ERRA("Process %d called exitMonitor outside of a monitor.", myID);
		exit(1);
	}

	/* go backwards in the stack of called monitors */
	processes[myID].currentMonitor--;

	if (--monitors[myMonitor].timesTaken == 0) {
		/* see if someone is waiting, and if yes, let the next process in */
		if (!isEmpty(&(monitors[myMonitor].entryList))) {
			int pid = removeHead(&(monitors[myMonitor].entryList));
			addLast(&readyList, pid);
			monitors[myMonitor].timesTaken = 1;
			monitors[myMonitor].takenBy = pid;
		} else {
			monitors[myMonitor].takenBy = -1;
		}
	}

	allowInterrupts();
}

void notify() {
	maskInterrupts();

	int myID = head(&readyList);
	int myMonitor = getCurrentMonitor(myID);

	if (myMonitor < 0) {
		ERRA("Process %d called notify outside of a monitor.", myID);
		exit(1);
	}

	if (!isEmpty(&(monitors[myMonitor].waitingList))) {
		int pid = removeHead(&monitors[myMonitor].waitingList);
		timedWaiting[pid] = 0;
		addLast(&monitors[myMonitor].entryList, pid);
	}

	allowInterrupts();
}

void notifyAll() {
	maskInterrupts();

	int myID = head(&readyList);
	int myMonitor = getCurrentMonitor(myID);

	if (myMonitor < 0) {
		ERRA("Process %d called notify outside of a monitor.", myID);
		exit(1);
	}

	while (!isEmpty(&(monitors[myMonitor].waitingList))) {
		int pid = removeHead(&monitors[myMonitor].waitingList);
		timedWaiting[pid] = 0;
		addLast(&monitors[myMonitor].entryList, pid);
	}
	allowInterrupts();
}


void idle_code() {
	unsigned int the_answer = 42;
	while(the_answer == 42) {
		// Everything is fine
	}
}
int createIdle() {
    int result = createSpecialProcess(&idle_code);
    return result;
}

// Clock process
void scheduler() {
	maskInterrupts();
	int i;

	// Enable clock interrupts
	init_clock();
	init_button();
	unsigned int time_since_last_commutation = 0;
	while(1) {
		int next_pid = !isEmpty(&readyList) ? head(&readyList) : idle_pid;
		iotransfer(processes[next_pid].p, 0);

		// Rising edge has happened!
		time_since_last_commutation += CLOCK_PERIOD;


		/* **********/
		/* CHECK 1  */
		/* **********/
		// Should we switch process? (scheduling part)
		if(time_since_last_commutation > TIME_SLICING_FREQUENCY) {
			int current = removeHead(&readyList);
			addLast(&readyList, current);
			time_since_last_commutation = 0;
		}

		/* **********/
		/* CHECK 2  */
		/* **********/
		/* All the processes that have called timedWait and have not been notified / kicked out
		 * are in the timedWaiting list.
		 * We check one by one that none of them has timed out */
		int dbg_proc_waiting = 0;
		for(i = 0; i < MAX_PROC ; ++i) { // considering MAX_PROC is small, not a big performance deal
			if (timedWaiting[i]) {
				dbg_proc_waiting++;
				int* timeout = &(processes[i].timeout);
				if(*timeout <= 0) {
					// Here, the process has timed out but was not notified
					continue;
				}

				*timeout -= CLOCK_PERIOD;


				if(*timeout <= 0) {

					/* If it is in a monitor's waiting list, remove from that list */
					int currMon = getCurrentMonitor(i);
					if (currMon >= 0 && monitors[currMon].takenBy != i) {

						int* list = &(monitors[currMon].waitingList);
						removeFromList(list, i);
						if (monitors[currMon].timesTaken > 0) {
							addLast(&monitors[currMon].entryList, i);
						} else {
							monitors[currMon].timesTaken = 1;
							monitors[currMon].takenBy = i;
							addFirst(&readyList, i);
						}
					} else {
						addFirst(&readyList, i);
					}
				}

			}
			
		}
	}
	allowInterrupts();
}
int createScheduler() {
	int result = createSpecialProcess(&scheduler);
	return result;
}

void waitInterrupt(int peripherique) {
	maskInterrupts();

	if(peripherique == 0) {
		ERR("Error, you are not allowed to wait clock interrupts ");
		exit(1);
	}
	int caller_pid = removeHead(&readyList);

	int next_pid = isEmpty(&readyList) ? idle_pid : head(&readyList);

	iotransfer(processes[next_pid].p, peripherique);

	// When we get back here, an interruption has happened :
	// we regive the CPU to the caller
	addFirst(&readyList, caller_pid);

	allowInterrupts();
}




void wait() {
	maskInterrupts();

	_wait();

	allowInterrupts();
}

void _wait() {
	int myID = head(&readyList);
	int myMonitor = getCurrentMonitor(myID);
	int myTaken;

	if (myMonitor < 0) {
		ERRA("Process %d called wait outside of a monitor.", myID);
		exit(1);
	}

	removeHead(&readyList);
	addLast(&monitors[myMonitor].waitingList, myID);

	/* save timesTaken so we can restore it later */
	myTaken = monitors[myMonitor].timesTaken;

	/* let the next process in, if any */
	if (!isEmpty(&(monitors[myMonitor].entryList))) {
		int pid = removeHead(&(monitors[myMonitor].entryList));
		addLast(&readyList, pid);
		monitors[myMonitor].timesTaken = 1;
		monitors[myMonitor].takenBy = pid;
	} else {
		monitors[myMonitor].timesTaken = 0;
		monitors[myMonitor].takenBy = -1;
	}
	checkAndTransfer();

	/* I am woken up by exitMonitor -- check if the monitor state is consistent */
	if ((monitors[myMonitor].timesTaken != 1) || (monitors[myMonitor].takenBy != myID)) {
		ERR("The kernel has performed an illegal operation. Please contact customer support.");
		exit(1);
	}

	/* we're back, restore timesTaken */
	monitors[myMonitor].timesTaken = myTaken;
}

int timedWait(int time) {
	maskInterrupts();
	
	if(time < 0) {
		ERR("[TimedWait] Please provide a valid timeout");
		exit(1);
	}
	
	int myPid = head(&readyList);
	int returnValue = 1;

	// Mark that the process is waiting
	processes[myPid].timeout = time;
	timedWaiting[myPid] = 1;

	wait();
	
	if(timedWaiting[myPid] != 0) {
		returnValue = 0;
		timedWaiting[myPid] = 0;
	}
	
	allowInterrupts();
	
	return returnValue;
}

void sleep(int time) {
	maskInterrupts();

	if(time < 0) {
		ERR("[sleep] Please provide a valid timeout");
		exit(1);
	}

	int myPid = removeHead(&readyList);

	timedWaiting[myPid] = 1;
	processes[myPid].timeout = time;

	if ( isEmpty(&readyList)) {
		transfer(processes[idle_pid].p);
	} else {
		transfer(processes[head(&readyList)].p);
	}
	//timedWaiting[myPid] = 0;

	allowInterrupts();
}
void start(){

	if(isEmpty(&readyList)) {
		ERR("No processes in the ready list! Exiting...");
		exit(1);
	}
	DPRINT("Starting kernel...");

	idle_pid = createIdle();
	scheduler_pid = createScheduler();

	//checkAndTransfer();
	transfer(processes[scheduler_pid].p);
}
