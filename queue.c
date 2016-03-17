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

#include <sys/types.h>
#include <sys/wait.h>

#define QUEUE_SIZE 1024
#define WORD_SIZE 4096


pthread_mutex_t lock;
pthread_mutex_t lock_w;

int working;
int onqueue;
int shutdown;

int qcount;
char queue[QUEUE_SIZE][WORD_SIZE];
int wcount;
int workerReady[QUEUE_SIZE];

int fq;
char* fdname = "/run/shm/";
char* fqsuffix = "queue.q";
char* fpsuffix = "queue.pid";
char fqname[256];
char fpname[256];

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
		char buffer[WORD_SIZE];
		buffer[WORD_SIZE-1] = '\0';
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
			pthread_mutex_unlock(&lock);
		}
		sleep(1);
	}
}

void *threadWorker (void * args)
{
	pthread_mutex_lock(&lock_w);
	params * p = (params*)args;
	int workerID = p->workerID;
	char* command = p->command;
	pthread_mutex_unlock(&lock_w);

	if (verbose) fprintf(stderr, "[INFO] Worker %d: %s\n",workerID,command);

	pid_t c_pid;
	int status;

	c_pid = fork();                                                                                                                                         
	if (c_pid == 0)
	{
		execl("/bin/bash", "bash", "-c", command, NULL);
		_exit(127);
	}
	else if (c_pid > 0)
	{
		waitpid(c_pid,&status,0);
	}
	else
	{
		if (verbose) fprintf(stderr, "[ERROR] Worker %d: Failed!\n",workerID);
	}

	pthread_mutex_lock(&lock);
	workerReady[workerID] = 1;
	pthread_mutex_unlock(&lock);

	if (verbose) fprintf(stderr, "[INFO] Worker %d: Finished\n",workerID);
}

int main ( int argc, char** argv )
{
	/* Arguments from CLI */
	int consumers = 3;
	char* command = "echo Hello";
	int nofinish = 0;
	int showhelp = 0;
	verbose = 0;

	int debug = 0;

	int c;
	opterr = 0;

	while ((c = getopt (argc, argv, "c:vp:nhd")) != -1)
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
			case 'd':
				debug = 1;
				verbose = 1;
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

	if (debug)
	{
		FILE* fp = fopen("/tmp/queue-debug.log","w");
		dup2(fileno(fp), STDERR_FILENO);
		fclose(fp);
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

	/* Declare names for control file & pipe */
	sprintf(fqname,"%s%d-%s",fdname,geteuid(),fqsuffix);
	sprintf(fpname,"%s%d-%s",fdname,geteuid(),fpsuffix);

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
			char buffsend[WORD_SIZE];
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
	if (fq == -1)
	{
		fprintf(stderr, "[ERROR] Could not create the queue!\n");
		return 1;
	}

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

	if (pthread_mutex_init(&lock, NULL) != 0 || pthread_mutex_init(&lock_w, NULL) != 0)
	{
		fprintf(stderr, "[ERROR] Mutex init failed\n");
		return 1;
	}
	
	int keep_looping = 1;
	while (nofinish || keep_looping)
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

			pthread_create(&(workers[wcount]),NULL,threadWorker,&p);
			sleep(1);	// FIXME - In some occasions, the pthread reads "p" after the memory position for "p" is removed
					// and rewritten with the new parameters of the next loop. The sleep and the following mutex is
					// to let the pthread get the token and read "p" properly.

			pthread_mutex_lock(&lock_w);
			wcount++;
			pthread_mutex_unlock(&lock_w);
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

		keep_looping = (onqueue > 0 || working > 0);

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
