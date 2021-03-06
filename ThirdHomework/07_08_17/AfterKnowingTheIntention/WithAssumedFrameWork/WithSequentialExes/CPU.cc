/*

	Somewhere around 07:30PM	
	dummi4 is not visible through the CMD, but that's for another time.
	
	08:00PM exact
	Things were running sequentially, but after 10 seconds(where nothing should anymore),

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

#define NUM_SECONDS 10

// make sure the asserts work
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

/*
** a signal handler for those signals delivered to this process, but
** not already handled.
*/
void grab (int signum) { dprint (signum); }

// c++decl> declare ISV as array 32 of pointer to function (int) returning
// void
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

/*
** an overloaded output operator that prints a PCB
*/
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

/*
** an overloaded output operator that prints a list of PCBs
*/
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

// http://www.cplusplus.com/reference/list/list/
list<PCB *> new_list;
list<PCB *> processes;

int sys_time;

int countOfTerm;

/*
**  send signal to process pid every interval for number of times.
*/
void send_signals (int signal, int pid, int interval, int number)
{
    dprintt ("at beginning of send_signals", getpid ());

    for (int i = 1; i <= number; i++)
    {
		kill(getppid(), SIGCONT);
        sleep (interval);
		//Now the process		

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
/*
**  SA_NOCLDSTOP
**  If  signum  is  SIGCHLD, do not receive notification when
**  child processes stop (i.e., when child processes  receive
**  one of SIGSTOP, SIGTSTP, SIGTTIN or SIGTTOU).
*/
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
	list<PCB*>::iterator iter;
	/*
		So, this is what Mr.Brian was talking about, having the 
		popped process be placed back at the end of the list,
	*/
	for(iter = processes.begin(); iter != processes.end(); iter++){
		PCB* process = *iter;
		if(process->state == READY || process->state == WAITING){
			PCB* toPutBack = process;
			processes.push_back(toPutBack);			
			processes.pop_front();
			return process;
		}
	}	
}

void scheduler (int signum)
{
    assert (signum == SIGALRM);
    sys_time++;

	while(new_list.size() != 0){
		PCB* newProcess = new (PCB);
		newProcess = new_list.back();
		newProcess->state = READY;
		processes.push_back(newProcess);
		new_list.pop_back();
	}
	
	while(countOfTerm != processes.size()){ //Actually how the code was originally looking like,				
		PCB* problemChild = choose_process();
		if(problemChild->state == WAITING){
			kill(problemChild->pid, SIGCONT);
		}	
		//f&e
		//If the line below is happening over and over again, I am pretty much 
		//starting a new process over and over again.
		if((problemChild->pid = fork())	== 0 && problemChild->state == READY){
			execl(problemChild->name, (char*)NULL);		
		}
		cout << "This particular process has: " << problemChild->pid << " as a PID" << endl;
		problemChild->state = RUNNING;		
		kill(getpid(), SIGSTOP);
		problemChild->state = WAITING;
		kill(problemChild->pid, SIGSTOP); // because sleep() in the send_signal will give enough time slice.
	}
}

void process_done (int signum)
{

    assert (signum == SIGCHLD);
    int status, cpid;
	//cpid is the pid of a child that just terminated.
    cpid = waitpid (-1, &status, WNOHANG);

	cout << cpid << " is the terminated pid" << endl;

	coutOfTerm++;

	list<PCB*>::iterator iter;
	for(iter = processes.begin(); iter != processes.end(); iter++){
		PCB* processToTerminate = *iter;
		if(processToTerminate->pid == cpid){
			processToTerminate->state = TERMINATED;
			processes.erase(iter);
			break;		
		}	
	}

    dprintt ("in process_done", cpid);
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

/*
** stop the running process and index into the ISV to call the ISR
*/
void ISR (int signum)
{
    if (kill (running->pid, SIGSTOP) == -1)
    {
        perror ("kill");
        return;
    }
    dprintt ("stopped", running->pid);
    ISV[signum](signum);
}

/*
** set up the "hardware"
*/
void boot (int pid)
{
    ISV[SIGALRM] = scheduler;       create_handler (SIGALRM, ISR);
    ISV[SIGCHLD] = process_done;    create_handler (SIGCHLD, ISR);

    // start up clock interrupt
    int ret;
    if ((ret = fork ()) == 0)
    {
        // signal this process once a second for three times
        send_signals (SIGALRM, pid, 1, NUM_SECONDS);

		//boot() contains a problem(not necessarily the cause of it) because 
		//kill(0, SIGTERM) isn't killing everything.	
        kill (0, SIGTERM);
    }

    if (ret < 0){ perror ("fork"); }
}

void create_idle ()
{
    int idlepid;

    if ((idlepid = fork ()) == 0)
    {
        dprintt ("idle", getpid ());

        // the pause might be interrupted, so we need to
        // repeat it forever.
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

    // create a process to soak up cycles
    create_idle ();
    running = idle;

    cout << running;

    // we keep this process around so that the children don't die and
    // to keep the IRQs in place.
    for (;;)
    {
        pause();
        if (errno == EINTR) { continue; }
        perror ("pause");
    }
}
