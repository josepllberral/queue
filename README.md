# queue

Version: 0.1
Date: 2015 February 27
Author: Josep Ll. Berral-Garcia
License: GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

## Description
Executes a command, allowing to queue others after it, or running up to k simultaneous and queuing the next ones. Sending commands to queue while another queue exists, send the new ones to the existing queue.

## Usage
```
$ queue -c shell_command -p simultaneous (default = 3) -v (verbose)
```

## Example
```
$ queue -c "echo Hello1; sleep 10; echo Bye1" -p 2
$ queue -c "echo Hello2; sleep 10; echo Bye2"
$ queue -c "echo Hello3; sleep 10; echo Bye3"
$ queue -c "echo Hello4; sleep 10; echo Bye4"
```

The first 'queue' creates the queue, with 2 running slots. The two first commands are executed immediately, while the third is not executed until one of the two firsts ends, and so on. The first queue is alive as far as there are commands in queue or running. After that, the queue is removed.

## Compile
Classical gcc with pthreads
```
$ gcc queue.c -o queue -pthread
```

## Future improvements/fixes:
- Circular queue (current only can receive X commands total)
- Variable sized queues
- Stack execution instead of queue
- Named queues in system
- Queues with priority
