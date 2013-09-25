#ifndef __DSH_H__         /* check if this header file is already defined elsewhere */
#define __DSH_H__

#include <stdio.h>
#include <sys/types.h>  /* pid_t */
#include <unistd.h>     /* getpid()*/
#include <signal.h>     /* signal name macros, and sig handlers */
#include <stdlib.h>     /* for exit() */
#include <errno.h>      /* for errno */
#include <sys/wait.h>   /* for WAIT_ANY */
#include <string.h>     /* strncpy */
#include <sys/stat.h>   /* file modes */
#include <fcntl.h>      /* file open */

/* Max length of input/output file name specified during I/O redirection */
#define MAX_LEN_FILENAME 80

/*Max length of the command line */
#define MAX_LEN_CMDLINE	120

#define MAX_ARGS 20 /* Maximum number of arguments to any command */

/*file descriptors for input and output; the range of fds are from 0 to 1023;
 * 0, 1, 2 are reserved for stdin, stdout, stderr */
#define INPUT_FD  1000
#define OUTPUT_FD 1001

#define MAX_HISTORY 20 /* flush the completed jobs after reaching the MAX_HISTORY */

#define MAX_ARGS 20 /* Maximum number of arguments to any command */

#define PRINT_INFO 1 /* FLAG for print_job() and other debug info */

/* using bool as built-in; char is better in terms of space utilization, but
 * code is not succint */
typedef enum { false, true } bool;

/* A process is a single process (a command to run an executable program).  */
typedef struct process {
        struct process *next;       /* next process in pipeline */
	    int argc;		            /* useful for free(ing) argv */
        char **argv;                /* for exec; argv[0] is the path of the executable file; argv[1..] is the list of arguments*/
        pid_t pid;                  /* process ID */
        bool completed;             /* true if process has completed */
        bool stopped;               /* true if process has stopped */
        int status;                 /* reported status value from job control; 0 on success and nonzero otherwise */
        char *ifile;                /* stores input file name when < is issued */
        char *ofile;                /* stores output file name when > is issued */
} process_t;

/* A job is a process itself or a pipeline of processes.
 * Each job has exactly one process group (pgid) containing all the processes in the job. 
 * Each process group has exactly one process that is its leader.
 */
typedef struct job {
        struct job *next;           /* next job */
        char *commandinfo;          /* entire command line input given by the user; useful for logging and message display*/
        process_t *first_process;   /* list of processes in this job */
        pid_t pgid;                 /* process group ID */
        bool notified;              /* true if user was informed about stopped job */
        int mystdin, mystdout, mystderr;  /* standard i/o channels */
        bool bg;                    /* true when & is issued on the command line */
} job_t;

/* Finds a job for which the pgid is still -1 (indicates not processed);
 * firt_job is the header to the job structure */
job_t *detach_job(job_t *first_job);

/* Return true if all processes in the job have stopped or completed.  */
bool job_is_stopped(job_t *j);

/* Return true if all processes in the job have completed.  */
bool job_is_completed(job_t *j);

/* Find the last job.  */
job_t *find_last_job();

/* delete a given job j; We will simply loop from first_job since we do not
 * store prev pointer */
void delete_job(job_t *j, job_t *first_job);

/* Initialize the members of job structure */
bool init_job(job_t *j);

/* Initialize the members of process structure */
bool init_process(process_t *p);

/* Prints the jobs in the list.  */
void print_job();

/* Bootstrapping for dsh shell for interactive mode */
void init_dsh();

 /* Grab control of the terminal for the calling process pgid.  */
void seize_tty(pid_t callingprocess_pgid);

/* checks whether haystack ends with needle */
int endswith(const char* haystack, const char* needle);

/* Basic parser that fills the data structures job_t and process_t defined in
 * dsh.h. We tried to make the parser flexible but it is not tested
 * with arbitrary inputs. Be prepared to hack it for the features
 * you may require. The more complicated cases such as parenthesis
 * and grouping are not supported. If the parser found some error, it
 * will always return NULL. 
 *
 * The parser supports these symbols: <, >, |, &, ;
 */

job_t* readcmdline(char *msg);

#ifdef NDEBUG
        #define DEBUG(M, ...)
#else
        #define DEBUG(M, ...) fprintf(stderr, "[DEBUG] %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#endif

#endif /* __DSH_H__*/
