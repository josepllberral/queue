/******************************************************************************/
/*                                                                            */
/* Command: queue                                                             */
/* Author: Josep Ll. Berral-Garcia                                            */
/* E-mail: josep.berral@bsc.es                                                */
/* Version: 0.1                                                               */
/* Date: 2015 February 27                                                     */
/* License: GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007                */
/*                                                                            */
/* Description: Executes a command, allowing to queue others after it, or     */
/*              running up to k simultaneous and queuing the next ones.       */
/*              Sending commands to queue while another queue exists, send    */
/*              the new ones to the existing queue.                           */
/*                                                                            */
/* Usage: $ queue -c shell_command -p simultaneous (default = 3) -v (verbose) */
/*                                                                            */
/* Example:                                                                   */
/* $ queue -c "echo Hello1; sleep 10; echo Bye1" -p 2                         */
/* $ queue -c "echo Hello2; sleep 10; echo Bye2"                              */
/* $ queue -c "echo Hello3; sleep 10; echo Bye3"                              */
/* $ queue -c "echo Hello4; sleep 10; echo Bye4"                              */
/*                                                                            */
/* The first 'queue' creates the queue, with 2 running slots. The two first   */
/* commands are executed immediately, while the third is not executed until   */
/* one of the two firsts ends, and so on. The first queue is alive as far as  */
/* there are commands in queue or running. After that, the queue is removed.  */
/*                                                                            */
/* Future improvements/fixes:                                                 */
/* - Circular queue (current only can receive X commands total)               */
/* - Variable sized queues                                                    */
/* - Stack execution instead of queue                                         */
/* - Named queues in system                                                   */
/* - Queues with priority                                                     */
/*                                                                            */
/******************************************************************************/

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define QUEUE_SIZE 256

int mutex;
int working;
int onqueue;
int shutdown;

int qcount;
char* queue[QUEUE_SIZE];
int workerReady[QUEUE_SIZE];

int fq;
char* fqname = "/run/shm/queue.q";
char* fpname = "/run/shm/queue.pid";

int verbose;

typedef struct
{
        int workerID;
        char* command;
} params;

void sig_handler(int signo)
{
	if (signo == SIGTERM)
	{
		if (verbose) fprintf(stderr, "[INFO] Received SIGTERM. Terminating...\n");
		if (access(fqname,F_OK) != -1) unlink(fqname);
		if (access(fpname,F_OK) != -1) remove(fpname);
	}
}

void *threadCheckQueue ()
{
	//printf("Hi, I'm Pinkie Pie and I threw this thread just for you!\n");

	while (shutdown == 0)
	{
		char buffer[1024];
		buffer[1023] = '\0';
		int l = read(fq, buffer, sizeof(buffer));
		if (l > 0)
		{
			if (verbose) fprintf(stderr, "[INFO] Received command \"%s\". OQ: %d, QC: %d\n", buffer, onqueue, qcount);

			while (mutex != 0); // Wait your turn
			mutex = 1;

			if (qcount < QUEUE_SIZE)
			{
				queue[qcount] = buffer;
				qcount++;
				onqueue++;
			}
			else
			{
				fprintf(stderr, "[ERROR] Exceded number of submissions :(");
			}

			mutex = 0;
		}
		sleep(1); // Do not "busywait" too much
	}
}

void *threadWorker (void * args)
{
	params * p = (params*)args;
	int workerID = p->workerID;
	char* command = p->command;

	if (verbose) fprintf(stderr, "[INFO] Worker %d: %s\n",workerID,command);
	system(command);

	while (mutex != 0); // Wait your turn
	mutex = 1;

	workerReady[workerID] = 1;

	mutex = 0;
	if (verbose) fprintf(stderr, "[INFO] Worker %d: Finished\n",workerID);
}

int main ( int argc, char** argv )
{
	if (argc < 2)
	{
		printf("Usage: %s command [-p #consumers] [-v]\nDefault #consumers = 3", argv[0]);
		return -1;
	}

	int consumers = 3;
	char* command = "echo Hello!";
	verbose = 0;

	int c;
	opterr = 0;

	while ((c = getopt (argc, argv, "c:vp:")) != -1)
	{
		switch (c)
		{
			case 'c':
				command = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'p':
				consumers = atoi(optarg);
				break;
			case '?':
				if (optopt == 'p')
					fprintf (stderr, "Option -%c requires an argument.\n", optopt);
				else if (isprint (optopt))
					fprintf (stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf (stderr,"Unknown option character `\\x%x'.\n",optopt);
				return 1;
			default:
				abort ();
		}
	}

	if (signal(SIGTERM, sig_handler) == SIG_ERR) fprintf(stderr, "[WARNING] Can't catch SIGTERM\n");

	if (access(fqname,F_OK) != -1)
	{
		/* Check if pipe is dead or alive */
		FILE* fp = fopen(fpname,"r");
		int buffpos = 0;
		char buffer[16];
		while ((buffer[buffpos++]=fgetc(fp)) != EOF);
		fclose(fp);

		if (kill(atoi(buffer), 0) != 0)
		{
			if (verbose) fprintf(stderr, "[INFO] Found old queue %d. Removing...\n", atoi(buffer));
			unlink(fqname);
			remove(fpname);
		}
		else
		{
			/* Commit command to existing queue */
			fq = open(fqname, O_WRONLY);
			char buffsend[1024];
			sprintf(buffsend,"%s",command);
			write(fq, buffsend, sizeof(buffsend));
			close(fq);
			fprintf(stderr, "[INFO] Sent command \"%s\" to running queue\n", command);

			return 0;
		}
	}

	/* Open control files & pipes */
	FILE* fp = fopen(fpname,"w");
	fprintf(fp, "%d",getpid());
	fclose(fp);

	mkfifo(fqname, 0600);
	fq = open(fqname, O_RDONLY | O_NONBLOCK);
	if (fq == -1) fprintf(stderr, "[ERROR] Could not open the queue!\n");

	/* Initialize variables */
	mutex = 0;		
	working = 0;
	onqueue = 0;
	qcount = 0;
	shutdown = 0;

	/* Put command on queue, as the 1st work */
	queue[qcount] = command;
	qcount++;
	onqueue++;

	/* Lauch Threads */
	pthread_t threadChecker;
	pthread_create(&(threadChecker),NULL,threadCheckQueue,NULL);

        pthread_t workers[QUEUE_SIZE];	// TODO - Create circular list!
	int workerID = 0;

	while (onqueue > 0 || working > 0)
	{
		while (mutex != 0); // Wait your turn
		mutex = 1;

		while ((working < consumers) && (onqueue > 0))
		{
			workerReady[workerID] = 0;
			working++;
			onqueue--;

			if (verbose) fprintf(stderr, "[INFO] Executing command \"%s\" from queue. WK: %d, OC: %d, WI: %d, QC: %d\n", queue[workerID], working, onqueue, workerID+1, qcount);

			params p;
			p.workerID = workerID;
			p.command = queue[workerID];
			pthread_create(&(workers[workerID++]),NULL,threadWorker,&p);
		}

		int i = 0;
		for (i = 0; i < workerID; i++)
		{
			if (workerReady[i] == 1)
			{
				pthread_join(workers[i],NULL);
				workerReady[i] = 0;
				working--;

				if (verbose) fprintf(stderr, "[INFO] Cleaning command \"%s\" from queue. WK: %d, OC: %d, WI: %d, QC: %d\n", queue[i], working, onqueue, workerID, qcount);
			}
		}

		mutex = 0;
		if (verbose) fprintf(stderr, "[INFO] WK: %d, OC: %d, WI: %d, QC: %d\n", working, onqueue, workerID, qcount);
		sleep(1); // Do not "busywait" too much
	}
	shutdown = 1;
	pthread_join(threadChecker,NULL);

	close(fq);
	unlink(fqname);

	remove(fpname);

        if (verbose) fprintf(stderr, "[INFO] Queue finished\n");

        return 0;
}
