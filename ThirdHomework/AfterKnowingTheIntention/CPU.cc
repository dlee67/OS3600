/*

	Under the assumption that the signal handler can't be called again, when
	it's under an automation.

*/
#include <iostream>
#include <list>
#include <iterator>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>

//The remaining 2 seconds should be idle time.
#define NUM_SECONDS 14

#undef NDEBUG
#include <assert.h>

#define EBUG
#ifdef EBUG
#   define dmess(a) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << a << endl;

#   define dprint(a) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << (#a) << " = " << a << endl;

#   define dprintt(a,b) cout << "in " << __FILE__ << \
    " at " << __LINE__ << " " << a << " " << (#b) << " = " \
    << b << endl
#else
#   define dprint(a)
#endif /* EBUG */

using namespace std;

enum STATE { NEW, RUNNING, WAITING, READY, TERMINATED };

void grab (int signum) { dprint (signum); }

void (*ISV[32])(int) = {
/*        00    01    02    03    04    05    06    07    08    09 */
/*  0 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 10 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 20 */ grab, grab, grab, grab, grab, grab, grab, grab, grab, grab,
/* 30 */ grab, grab
};

struct PCB
{
    STATE state;
    const char *name;   // name of the executable
    int pid;            // process id from fork();
    int ppid;           // parent process id
    int interrupts;     // number of times interrupted
    int switches;       // may be < interrupts
    int started;        // the time this process started
};

ostream& operator << (ostream &os, struct PCB *pcb)
{
    os << "state:        " << pcb->state << endl;
    os << "name:         " << pcb->name << endl;
    os << "pid:          " << pcb->pid << endl;
    os << "ppid:         " << pcb->ppid << endl;
    os << "interrupts:   " << pcb->interrupts << endl;
    os << "switches:     " << pcb->switches << endl;
    os << "started:      " << pcb->started << endl;
    return (os);
}

ostream& operator << (ostream &os, list<PCB *> which)
{
    list<PCB *>::iterator PCB_iter;
    for (PCB_iter = which.begin(); PCB_iter != which.end(); PCB_iter++)
    {
        os << (*PCB_iter);
    }
    return (os);
}

PCB *running;
PCB *idle;

list<PCB *> new_list;
list<PCB *> processes;

int sys_time;
int countOfTerm;

void create_idle ()
{
    int idlepid;

    if ((idlepid = fork ()) == 0)
    {
        dprintt ("idle", getpid ());

        for (;;)
        {
            dmess ("going to sleep");
            pause ();
            if (errno == EINTR)
            {
                dmess ("waking up");
                continue;
            }
            perror ("pause");
        }
    }
    idle = new (PCB);
    idle->state = RUNNING;
    idle->name = "IDLE";
    idle->pid = idlepid;
    idle->ppid = 0;
    idle->interrupts = 0;
    idle->switches = 0;
    idle->started = sys_time;
}

void send_signals (int signal, int pid, int interval, int number)
{
    dprintt ("at beginning of send_signals", getpid ());

    for (int i = 1; i <= number; i++)
    {	        
		sleep (interval);
		kill(pid, SIGCONT);	
	
        dprintt ("sending", signal);
        dprintt ("to", pid);

        if (kill (pid, signal) == -1)
        {
            perror ("kill");
            return;
        }
    }

    dmess ("at end of send_signals");
}

struct sigaction *create_handler (int signum, void (*handler)(int))
{
    struct sigaction *action = new (struct sigaction);

    action->sa_handler = handler;

    if (signum == SIGCHLD)
    {
        action->sa_flags = SA_NOCLDSTOP;
    }
    else
    {
        action->sa_flags = 0;
    }

    sigemptyset (&(action->sa_mask));

    assert (sigaction (signum, action, NULL) == 0);
    return (action);
}

PCB* choose_process ()
{
	int countOfReady = 0;
	list<PCB*>::iterator iter;
//This needs to iterate from the beginning of the process, after all the processes have 
//been processes at least once.
	for(iter = processes.begin(); countOfTerm != processes.size(); iter++){

		if(countOfReady == processes.size()){

			iter = processes.begin();

		}

		PCB* process = *iter; 

		if(process->state == READY){
			/*
				int letItRun = fork();
				if(letItRun == 0){
					execl(process->name, NULL);
				}
			process->state = RUNNING;
			process->pid = letItRun; 
			process->ppid = getpid();
			countOfReady++;
			*/
			return process;		
		}
	}	
}

void scheduler (int signum)
{
    assert (signum == SIGALRM);
    sys_time++;
	running->state = READY;
	running->interrupts += 1;

	while(new_list.size() != 0){

		PCB* newProcess = new (PCB);
		newProcess = new_list.back();
		processes.push_back(newProcess);
		new_list.pop_back();

	}

	while(countOfTerm != processes.size()){

		PCB* toFork = choose_process();
		toFork->state = WAITING;
		toFork->ppid = getpid();
		toFork->pid = fork();

		if(toFork->pid == 0){
			execl(toFork->name, NULL);
		}	
		//When SIGALRM arrives, the above process stops.
		//So, should I make a handler or something...?
		kill(toFork->pid, SIGSTOP);
		kill(getpid(), SIGSTOP);		
	}
}

//Must some how associated the terminated process with the function below, each time it ends.
//When this whole body of code gets commented out, the a new process does not start.
//But why?
void process_done (int signum)
{
    assert (signum == SIGCHLD);

	countOfTerm++;

    int status, cpid;

    cpid = waitpid (-1, &status, WNOHANG);

    dprintt ("in process_done", cpid);

	dprintt("number of context switched: ", running->switches);
	dprintt("number of interrupts: ", running->interrupts);
	dprintt("number of sys_time: ", sys_time);

    if  (cpid == -1)
    {
        perror ("waitpid");
    }
    else if (cpid == 0)
    {
        if (errno == EINTR) { return; }
        perror ("no children");
    }
    else
    {
        dprint (WEXITSTATUS (status));
    }

}


void ISR (int signum)
{
	//I think the line below refers to the idle, not the root parent.
	//Since, the running became idle in the main.
    if (kill (running->pid, SIGSTOP) == -1)
    {
        perror ("kill");
        return;
    }
    dprintt ("stopped", running->pid);

    ISV[signum](signum);
}

void boot (int pid)
{
    ISV[SIGALRM] = scheduler;       create_handler (SIGALRM, ISR);
    ISV[SIGCHLD] = process_done;    create_handler (SIGCHLD, ISR);

    int ret;
    if ((ret = fork ()) == 0)
    {

        send_signals (SIGALRM, pid, 1, NUM_SECONDS);
        kill (0, SIGTERM);
    }

    if (ret < 0)
    {

        perror ("fork");

    }
}

int main (int argc, char **argv)
{
    int pid = getpid();
    dprintt ("main", pid);

    sys_time = 0;

	int n = 1;
	
	while(argv[n]){

		PCB* newProcess = new (PCB);
		newProcess->name = argv[n];
		newProcess->state = READY;
		new_list.push_back(newProcess);		
		n++;

	}

    boot (pid);

    create_idle ();
    running = idle;

    cout << running;

    for (;;)
    {
        pause();
        if (errno == EINTR) { continue; }
        perror ("pause");
    }
}
