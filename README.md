# queue

Version: 0.1

Date: 2015 February 27

Author: Josep Ll. Berral-Garcia

License: GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

## Description
Executes a command, allowing to queue others after it, or running up to _k_ simultaneous and queuing the next ones. Sending commands to queue while another queue exists will dispatch those to the existing queue, also from different ttys.

## Usage
```
$ queue -c shell_command [OPTIONS]

-c command Command to be executed or to be put in queue.
-p <value> Maximum number of simultaneuos commands.
-v         Displays information and debug messages.
-n         Queue is alive and ready after finishing current commands.
-h         Shows the help message and finishes.
```

## Example
```
$ queue -c "echo Command1; sleep 10; echo EndCommand1" -p 2 &
$ queue -c "echo Command2; sleep 10; echo EndCommand2"
$ queue -c "echo Command3; sleep 10; echo EndCommand3"
$ queue -c "echo Command4; sleep 10; echo EndCommand4"
```

The first 'queue' creates the queue, with 2 running slots. The two first commands are executed immediately, while the third is not executed until one of the two firsts ends, and so on. The first queue is alive as far as there are commands in queue or running. After that, the queue is removed.

If option "-n" is added to the first command, the queue will not end and will remain listening for new commands, or expecting a SIGTERM (please, be polite).

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
- Loggin commands

