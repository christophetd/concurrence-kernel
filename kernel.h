#ifndef KERNEL_H_
#define KERNEL_H_

void createProcess(void (*f)(), int stackSize);

void start();

int createMonitor();

void enterMonitor(int monitorID);

void exitMonitor();

void wait();

void notify();

void notifyAll();

void yield();

int createEvent();

void attendre(int eventID);

void declencher(int eventID);

void reinitialiser(int eventID);

#endif /*KERNEL_H_*/
