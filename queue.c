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
/*              the new ones to the existing queue, also from different ttys. */
/*                                                                            */
/* Usage: $ queue -c shell_command -p simultaneous (default = 3) -v (verbose) */
/*                -n (don't die with the last command and wait for new ones)  */
/*                -h (display help)                                           */
/*                                                                            */
/* For more information: https://github.com/josepllberral/queue               */
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

pthread_mutex_t lock;

int working;
int onqueue;
int shutdown;

int qcount;
char queue[QUEUE_SIZE][1024];
int wcount;
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
			pthread_mutex_lock(&lock);
			if (verbose) fprintf(stderr, "[INFO] Received command \"%s\" from other queue call\n", buffer);
			if (qcount < QUEUE_SIZE)
			{
				sprintf(queue[qcount],"%s",buffer);
				qcount++;
				onqueue++;
			}
			else
			{
				fprintf(stderr, "[ERROR] Exceded number of submissions :(");
			}

			int i = 0;
			for (i=0;i <qcount; i++)
			{
				fprintf(stderr, "QUEUE %d: %s\n", i,queue[i]);
			}

			pthread_mutex_unlock(&lock);
		}
		sleep(1);
	}
}

void *threadWorker (void * args)
{
	params * p = (params*)args;
	int workerID = p->workerID;
	char* command = p->command;

	if (verbose) fprintf(stderr, "[INFO] Worker %d: %s\n",workerID,command);
	system(command);

	pthread_mutex_lock(&lock);
	workerReady[workerID] = 1;
	pthread_mutex_unlock(&lock);

	if (verbose) fprintf(stderr, "[INFO] Worker %d: Finished\n",workerID);
}

int main ( int argc, char** argv )
{
	/* Arguments from CLI */
	int consumers = 3;
	char* command = "echo Hello!";
	int nofinish = 0;
	int showhelp = 0;
	verbose = 0;

	int c;
	opterr = 0;

	while ((c = getopt (argc, argv, "c:vp:nh")) != -1)
	{
		switch (c)
		{
			case 'c':
				command = optarg;
				break;
			case 'v':
				verbose = 1;
				break;
			case 'n':
				nofinish = 1;
				break;
			case 'h':
				showhelp = 1;
				break;
			case 'p':
				consumers = atoi(optarg);
				break;
			case '?':
				if (optopt == 'p')
					fprintf (stderr, "Option -%c requires an argument\n", optopt);
				else if (isprint (optopt))
					fprintf (stderr, "Unknown option `-%c'\n", optopt);
				else
					fprintf (stderr,"Unknown option character `\\x%x'\n",optopt);
				return 1;
			default:
				abort ();
		}
	}

	if (argc < 2 || showhelp)
	{
		fprintf(stderr, "Usage: %s -c command [options]\n\n", argv[0]);
		fprintf(stderr, "          -c command Command to be executed or to be put in queue.\n");
		fprintf(stderr, "          -p <value> Maximum number of simultaneuos commands.\n");
		fprintf(stderr, "          -v         Displays information and debug messages.\n");
		fprintf(stderr, "          -n         Queue is alive and ready after finishing current commands.\n");
		fprintf(stderr, "          -h         Shows this help and finishes.\n");
		fprintf(stderr, "\nMain site for '%s': https://github.com/josepllberral/queue\n",argv[0]);

		if (argc < 2) return -1; else return 0;
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
			if (verbose) fprintf(stderr, "[INFO] Found dead queue at [%d]. Removing...\n", atoi(buffer));
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
			fprintf(stderr, "[INFO] Sent command \"%s\" to running queue at [%d]\n", command, atoi(buffer));

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
	working = 0;
	wcount = 0;
	onqueue = 0;
	qcount = 0;
	shutdown = 0;

	/* Put command on queue, as the 1st work */
	sprintf(queue[qcount],"%s",command);
	qcount++;
	onqueue++;

	/* Launch Threads */
	pthread_t threadChecker;
	pthread_create(&(threadChecker),NULL,threadCheckQueue,NULL);

        pthread_t workers[QUEUE_SIZE];	// TODO - Create circular list!

	if (pthread_mutex_init(&lock, NULL) != 0)
	{
		printf("[ERROR] Mutex init failed\n");
		return 1;
	}
	
	while (nofinish || (onqueue > 0 || working > 0))
	{
		pthread_mutex_lock(&lock);

		while ((working < consumers) && (onqueue > 0))
		{
			workerReady[wcount] = 0;
			working++;
			onqueue--;

			if (verbose) fprintf(stderr, "[INFO] Executing command \"%s\" from queue\n",queue[wcount]);
			if (verbose) fprintf(stderr, "[INFO] WK: %d, OC: %d, WI: %d, QC: %d\n", working, onqueue, wcount+1, qcount);

			params p;
			p.workerID = wcount;
			p.command = queue[wcount];
			pthread_create(&(workers[wcount++]),NULL,threadWorker,&p);
		}

		int i = 0;
		for (i = 0; i < wcount; i++)
		{
			if (workerReady[i] == 1)
			{
				pthread_join(workers[i],NULL);
				workerReady[i] = 0;
				working--;

				if (verbose) fprintf(stderr, "[INFO] Cleaning command \"%s\" from queue\n", queue[i]);
				if (verbose) fprintf(stderr, "[INFO] WK: %d, OC: %d, WI: %d, QC: %d\n", working, onqueue, wcount, qcount);
			}
		}

		pthread_mutex_unlock(&lock);
		sleep(1);
	}
	shutdown = 1;
	pthread_join(threadChecker,NULL);

	pthread_mutex_destroy(&lock);

	close(fq);
	unlink(fqname);

	remove(fpname);

        if (verbose) fprintf(stderr, "[INFO] Queue finished\n");

        return 0;
}
