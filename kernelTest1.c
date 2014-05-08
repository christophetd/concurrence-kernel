#include <stdio.h>
#include <stdlib.h>
#include "system.h"
#include "altera_avalon_pio_regs.h"
#include "kernel.h"

#define STACK_SIZE	10000
#define BLINKS		4
#define PAUSE		500000

/*********************** Buffer implemented using monitors *********************/
typedef struct {
	int message;
	int full;
	int monitor;
} Buffer;

void initBuffer(Buffer* b) {
	b->monitor = createMonitor();
	b->full = 0;
}

void put(Buffer* b, int m) {
	enterMonitor(b->monitor);
	while(b->full) {
		wait();
	}
	b->message = m;
	b->full = 1;
	notify();
	exitMonitor();
	return;
}

int get(Buffer*b) {
	int m;

	enterMonitor(b->monitor);
	while(!b->full) {
		wait();
	}
	m = b->message;
	b->full = 0;
	notifyAll();
	exitMonitor();

	return m;
}
/*****************************************************************************/

/*********************** Buffer implemented using events *********************/
typedef struct {
	int message;
	int fullEvent;
	int emptyEvent;
} EventBuffer;

void initEventBuffer(EventBuffer* b) {
	b->emptyEvent = createEvent();
	b->fullEvent = createEvent();
	declencher(b->emptyEvent);
}

void eput(EventBuffer* b, int m) {
	attendre(b->emptyEvent);
	reinitialiser(b->emptyEvent);
	b->message = m;
	declencher(b->fullEvent);
}

int eget(EventBuffer *b) {
	int m;
	attendre(b->fullEvent);
	reinitialiser(b->fullEvent);
	m = b->message;
	declencher(b->emptyEvent);
	return m;
}
/*****************************************************************************/

/* buffers used by the program */
Buffer b0, b1;
EventBuffer b2;

/* dummy monitors used only for testing nested calls; they do not do any useful work */
int dummyMonitor1, dummyMonitor2;

/* arrays used to convert digits to their LCD representations */
int digitCodes[] = {0x3E223E00, 0x203E2400, 0x2E2A3A00, 0x3E2A2A00, 0x3E080E00,
		0x3A2A2E00, 0x3A2A3E00, 0x3E020200, 0x3E2A3E00, 0x3E2A2E00, 0};
int lcdZones[] = {LED_0_BASE, LED_1_BASE, LED_2_BASE};

/* displays number "no" in LCD zone "zone" */
void displayNumber(int zone, int no) {
	IOWR_ALTERA_AVALON_PIO_DATA(lcdZones[zone], digitCodes[no]);
}

/* blinks number "no" in LCD zone "zone" */
void blinkNumber(int zone, int no) {
	int i, j;
	for (i = 0; i < BLINKS; i++) {
		IOWR_ALTERA_AVALON_PIO_DATA(lcdZones[zone], 0);
		for (j = 0; j < PAUSE; j++);
		IOWR_ALTERA_AVALON_PIO_DATA(lcdZones[zone], digitCodes[no]);
		for (j = 0; j < PAUSE; j++);
	}
}

void producer(){
	int reg, temp;

	printf("Producer starting...\n");

	reg = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE);
	IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE,reg);

	while(1) {
		enterMonitor(dummyMonitor1);
		enterMonitor(dummyMonitor2);
		enterMonitor(dummyMonitor1);
		reg = IORD_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE);
		if (reg != 0) {

			/* check button 0 */
			temp = reg;
			if (temp%2==1) {
				printf("putting to 0\n");
				put(&b0, 0);
			}

			/* check button 1 */
			temp = temp >> 1;
			if (temp%2==1) {
				printf("putting to 1\n");
				put(&b1, 1);
			}

			/* check button 2 */
			temp = temp >> 1;
			if (temp%2==1) {
				printf("putting to 2\n");
				exitMonitor();
				exitMonitor();
				exitMonitor();
				eput(&b2, 2);
				enterMonitor(dummyMonitor1);
				enterMonitor(dummyMonitor2);
				enterMonitor(dummyMonitor1);
			}

			/* check button 3 -- exit if pressed */
			temp = temp >> 1;
			if (temp%2==1) {
				printf("Bye!\n");
				displayNumber(0, 10);
				displayNumber(1, 10);
				displayNumber(2, 10);
				exit(0);
			}

			IOWR_ALTERA_AVALON_PIO_EDGE_CAP(BUTTONS_BASE,reg); /* reset buttons to 0 */
		}
		exitMonitor();
		exitMonitor();
		exitMonitor();
		yield();
	}
}

void consumer0(){
	int counter = 0;
	int m;

	displayNumber(0, 0);
	printf("Consumer 0 starting...\n");
	while(1) {
		m = get(&b0);
		printf("consumed from 0\n");
		blinkNumber(0, counter);
		counter = (counter + 1) % 10;
	 	displayNumber(0, counter);
	}
}

void consumer1(){
	int counter = 0;
	int m;

	displayNumber(1, 0);
	printf("Consumer 1 starting...\n");
	while(1) {
		m = get(&b1);
		printf("consumed from 1\n");
		blinkNumber(1, counter);
		counter = (counter + 1) % 10;
		displayNumber(1, counter);
	}
}

void consumer2(){
	int counter = 0;
	int m;

	displayNumber(2, 0);
	printf("Consumer 2 starting...\n");
	while(1) {
		m = eget(&b2);
		printf("consumed from 2\n");
		blinkNumber(2, counter);
		counter = (counter + 1) % 10;
		displayNumber(2, counter);
	}
}
#define CLEAR(i) IOWR_ALTERA_AVALON_PIO_DATA(lcdZones[i], 0)
#define PRINT(i) IOWR_ALTERA_AVALON_PIO_DATA(lcdZones[i], digitCodes[i])
void test0() {
	int j;
	while (1) {
		enterMonitor(dummyMonitor1);
		wait();
		CLEAR(0);
		yield();
		for (j = 0; j < PAUSE; j++);
		wait();
		PRINT(0);
		yield();
		exitMonitor();
		for (j = 0; j < PAUSE; j++);
	}
}

void test1() {
	int j;
	while (1) {
		enterMonitor(dummyMonitor2);
		enterMonitor(dummyMonitor1);
		yield();
		notifyAll();
		yield();
		CLEAR(1);
		yield();
		for (j = 0; j < PAUSE; j++);
		yield();
		PRINT(1);
		yield();
		exitMonitor();
		exitMonitor();
		for (j = 0; j < PAUSE; j++);
	}
}

void test2() {
	int j;
	while (1) {
		enterMonitor(dummyMonitor2);
		yield();
		CLEAR(2);
		for (j = 0; j < PAUSE; j++);
		yield();
		PRINT(2);
		exitMonitor();
		for (j = 0; j < PAUSE; j++);
	}
}

int main() {
	IOWR_ALTERA_AVALON_PIO_DATA(LED_COLOR_BASE, LED_COLOR_RESET_VALUE);
	initBuffer(&b0);
	initBuffer(&b1);
	initEventBuffer(&b2);
	dummyMonitor1 = createMonitor();
	dummyMonitor2 = createMonitor();

	createProcess(consumer0, STACK_SIZE);
	createProcess(consumer1, STACK_SIZE);
	createProcess(consumer2, STACK_SIZE);
	createProcess(producer, STACK_SIZE);

	start();
	return 0;
}

//int main(void) {
//	IOWR_ALTERA_AVALON_PIO_DATA(LED_COLOR_BASE, LED_COLOR_RESET_VALUE);
//
//	dummyMonitor1 = createMonitor();
//	dummyMonitor2 = createMonitor();
//
//	createProcess(test0, STACK_SIZE);
//	createProcess(test1, STACK_SIZE);
//	createProcess(test2, STACK_SIZE);
//
//
//	start();
//	return 0;
//}
