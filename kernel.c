#include <stdio.h>
#include <stdlib.h>

#include "system_m.h"

#define FLUSH {int i; for(i = 0 ; i < 500000 ; i++) {}}

// Maximum number of processes.
#define MAXPROCESS 10

// Maximum number of monitors
#define MAXMONITORS 10

// Maximum number of monitors
#define MAXEVENTS 10

// Pointer to the head of list of ready processes
int readyList = -1;

typedef struct {
    int next;
    Process p;
    int monitorsStack[MAXMONITORS];
    unsigned int monitorsStackPos;
} ProcessDescriptor;

typedef struct {
	short isOccupied;
	int waitingList; // processes waiting to be notified
	int readyList; // processes waiting for the monitor to be free
	/*
	 * When notify (notifyAll) is called, it moves one (all) process(es) from
	 * the waiting list to the ready list.
	 */
} Monitor;

ProcessDescriptor processes[MAXPROCESS];
int nextProcessId = 0;

/* Monitors */
int nextMonitorId = 0;
Monitor monitors[MAXMONITORS];

/***********************************************************
 ***********************************************************
            Utility functions for list manipulation
 ***********************************************************
 ***********************************************************/

// add element to the tail of the list
void addLast(int* list, int processId) {

    if (*list == -1){
        // list is empty
        *list = processId;
    }
    else {
        int temp = *list;
        while (processes[temp].next != -1){
            temp = processes[temp].next;
        }
        processes[temp].next = processId;
        processes[processId].next = -1;
    }

}

// add element to the head of list
void addFirst(int* list, int processId){

    if (*list == -1){
        *list = processId;
    }
    else {
        processes[processId].next = *list;
        *list = processId;
    }
}

// remove element that is head of the list
int removeHead(int* list){
    if (*list == -1){
        printf("List is empty!");
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

int size(int* list) {
	int i;
	for (i=0 ; *list != -1 ; i++) {
		list = &processes[*list].next;
	}

	return i;
}

// returns head of the list
int head(int* list){
    if (*list == -1){
        printf("List is empty!\n");
        return(-1);
    }
    else {
        return *list;
    }
}

/* Pointer to the head of list of ready processes
int readyList = -1;

typedef struct {
    int next;
    Process p;
} ProcessDescriptor;

ProcessDescriptor processes[MAXPROCESS];
int nextProcessId = 0;*/


/***********************************************************
 ***********************************************************
                    Kernel functions
 ***********************************************************
 ***********************************************************/

void createProcess (void (*f), int stackSize) {
	if (nextProcessId == MAXPROCESS){
		printf("Error: Maximum number of processes reached!\n");
		exit(1);
	}

	Process process;
	unsigned int* stack = malloc(stackSize);
	process = newProcess(f, stack, stackSize);
	processes[nextProcessId].next = -1;
	processes[nextProcessId].p = process;
	processes[nextProcessId].monitorsStackPos = 0;
	// add process to the list of ready Processes
	addLast(&readyList, nextProcessId);
	nextProcessId++;
}

inline int currentProcess(void) {
	return head(&readyList);
}

inline int currentMonitorID(void) {
	printf(" currentMonitorID: currentProcess = %d\n", currentProcess());
	FLUSH
	ProcessDescriptor process = processes[currentProcess()];
	printf(" currentMonitorID: stackPos = %d\n", process.monitorsStackPos);
	FLUSH
	return process.monitorsStack[process.monitorsStackPos-1];
}

inline Monitor currentMonitor(void) {
	return monitors[currentMonitorID()];
}

void start() {
    printf("Starting kernel...\n");
    if (readyList == -1){
        printf("Error: No process in the ready list!\n");
        exit(1);
    }
    Process process = processes[head(&readyList)].p;
    transfer(process);
}


int createMonitor() {
	if(nextMonitorId == MAXMONITORS) {
		printf("Error: maximum number of monitors reached!\n");
		exit(1);
	}

	monitors[nextMonitorId].isOccupied 	= 0;
	monitors[nextMonitorId].readyList	= -1;
	monitors[nextMonitorId].waitingList = -1;

	return nextMonitorId++;
}

void enterMonitor(int monitorID) {
	printf("#begin enterMonitor\n");
	// Waiting on the monitor
	if (monitors[monitorID].isOccupied) {
		// gives CPU to first process on ready list
		addFirst(&monitors[monitorID].readyList, removeHead(&readyList));
		transfer(processes[head(&readyList)].p);
    }
    // The guy who unfroze us put us on top of ready list and was "occupying" the monitor

    // Actually entering monitor
    // monitor-level stuff
    monitors[monitorID].isOccupied = 1;
    ProcessDescriptor process = processes[currentProcess()];
    // process-level stuff
    const int pos = process.monitorsStackPos;
    process.monitorsStack[pos] = monitorID;
	process.monitorsStackPos ++;
	printf("#end enterMonitor, proc: %d, stackPos: %d\n", currentProcess(), processes[currentProcess()].monitorsStackPos);
}

void exitMonitor(void) {
	printf("#begin exitMonitor, stackPos: %d\n", processes[currentProcess()].monitorsStackPos);
	Monitor mon = currentMonitor();
	processes[currentProcess()].monitorsStackPos --;
	mon.isOccupied = 0;

	// Check ready list for waiting processes
	int* const myList = &mon.readyList;
	if (*myList != -1) {
		int nextProcessID = removeHead(myList);
		printf(" %d is waiting on this monitor\n", nextProcessID);
		FLUSH
		// Put the process on the 'global' ready list
		addFirst(&readyList, nextProcessID);
		transfer(processes[nextProcessID].p);
	}
	// Doesn't do anything after transfer
	printf("#end exitMonitor\n");
}

void wait(void) {
	Monitor mon = currentMonitor();
	mon.isOccupied = 0;

	// Stop current process
	addFirst(&mon.waitingList, removeHead(&readyList));


	// Find a candidate process to run
	int* const myList = &mon.readyList;
	// candidate is ready on local monitor
	if (*myList != -1) {
		int nextProcessID = removeHead(myList);
		// Put the process on the 'global' ready list
		addFirst(&readyList, nextProcessID);
		transfer(processes[nextProcessID].p);
	}
	// no candidate on local monitor, pick one in global ready list
	else {
		transfer(processes[head(&readyList)].p);
	}
	// at this point, the monitor HAS to be free & we have been notified

	// we re-take the monitor
	mon.isOccupied = 1;
}

/* private */ void _notify(Monitor mon) {
	int notified = removeHead(&mon.waitingList);
	addFirst(&mon.readyList, notified);
}

void notify(void) {
	Monitor mon = currentMonitor();
	if (mon.waitingList != -1) {
		_notify(mon);
	}
}

void notifyAll(void) {
	Monitor mon = currentMonitor();
	while (mon.waitingList != -1) {
		_notify(mon);
	}
}

void yield(void){
	printf("#yield, stack pos is: %d\n", processes[currentProcess()].monitorsStackPos);
    int pId = removeHead(&readyList);
    addLast(&readyList, pId);
    printf("process %d yielding to %d\n", pId, head(&readyList));
    Process process = processes[head(&readyList)].p;
    transfer(process);
    printf("#endYield, stack pos is: %d\n", processes[currentProcess()].monitorsStackPos);
}

int createEvent() {
	return 0;
}

void attendre(int eventID) {

}

void declencher(int eventID) {

}

void reinitialiser(int eventID) {

}
