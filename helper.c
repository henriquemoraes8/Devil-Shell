#include "dsh.h"

pid_t dsh_pgid;         /* process group id of dsh */
int dsh_terminal_fd;    /* terminal file descriptor of dsh */
int dsh_is_interactive; /* interactive or batch mode */

/* Return true if all processes in the job have stopped or completed.  */
bool job_is_stopped(job_t *j) 
{

	process_t *p;
	for(p = j->first_process; p; p = p->next)
		if(!p->completed && !p->stopped)
	    		return false;
	return true;
}

/* Return true if all processes in the job have completed.  */
bool job_is_completed(job_t *j) 
{

	process_t *p;
	for(p = j->first_process; p; p = p->next)
		if(!p->completed)
	    		return false;
	return true;
}

/* Find the last job.  */
job_t *find_last_job(job_t *first_job) {
    job_t *j = first_job;
    if(!j) return NULL;
    while(j->next != NULL)
        j = j->next;
    return j;
}

/* Find the job for which the pgid is still -1 (indicates not processed) */
job_t *detach_job(job_t *first_job) 
{
	job_t *j = first_job;
	if(!j) return NULL;
	while(j->pgid != -1 && j->next != NULL)
		j = j->next;
	if(j->pgid == -1)
		return j;
	return NULL;
}

/* free_job iterates and invokes free on all its members */
bool free_job(job_t *j) 
{
	if(!j)
		return true;
	free(j->commandinfo);
	process_t *p;
	for(p = j->first_process; p; p = p->next) {
		int i;
		for(i = 0; i < p->argc; i++)
			free(p->argv[i]);
		free(p->argv);
        	free(p->ifile);
        	free(p->ofile);
	}
	free(j);
	return true;
}


/* delete a given job j; We will simply loop from first_job since we do not
 * store prev pointer */
void delete_job(job_t *j, job_t *first_job) 
{
	if(!j || !first_job)
		return;

	job_t *first_job_itr = first_job;

	if(first_job_itr == j) {	/* header */
		free_job(j);
		first_job = NULL; 	/* reinitialize for later purposes */
		return;
	}

	/* not header */
	while(first_job_itr->next != NULL) {
		if(first_job_itr->next == j) {
			first_job_itr->next = first_job_itr->next->next;
			free_job(j);
			return;
		}
		first_job_itr = first_job_itr->next;
	}
}

/* checks whether haystack ends with needle */
int endswith(const char* haystack, const char* needle) 
{
	size_t hlen;
	size_t nlen;
	/* find the length of both arguments - 
	if needle is longer than haystack, haystack can't end with needle */
	hlen = strlen(haystack); 
	nlen = strlen(needle);
	if(nlen > hlen) return 0;

	/* see if the end of haystack equals needle */
	return (strcmp(&haystack[hlen-nlen], needle)) == 0;
}

void seize_tty(pid_t callingprocess_pgid)
{
	/* Grab control of the terminal.  */
	/* Don't call this until other initialization is complete */
	if(tcsetpgrp(STDIN_FILENO, callingprocess_pgid) < 0) {
		perror("tcsetpgrp failure");
		exit(EXIT_FAILURE);
	}
}

/* If dsh is running interactively as the foreground job
 * then set the process group and signal handlers
 * */

void init_dsh() 
{

	dsh_terminal_fd = STDIN_FILENO;

	/* isatty(): test whether a file descriptor refers to a terminal 
	 * isatty() returns 1 if fd is an open file descriptor referring to a
 	 * terminal; otherwise 0 is returned, and errno is set to indicate the error.
 	 * */
	dsh_is_interactive = isatty(dsh_terminal_fd);

  	/* See if we are running interactively.  */
	if(dsh_is_interactive) {
		/* Loop until we are in the foreground.  */
		while(tcgetpgrp(dsh_terminal_fd) != (dsh_pgid = getpgrp()))
			kill(- dsh_pgid, SIGTTIN); /* Request for terminal input */

		/* Ignore interactive and job-control signals.
		* If tcsetpgrp() is called by a member of a background process 
		* group in its session, and the calling process is not blocking or
		* ignoring SIGTTOU,  a SIGTTOU signal is sent to all members of 
		* this background process group.
		*/
		signal(SIGTTOU, SIG_IGN);

		/* Put the dsh in our own process group.  */
		dsh_pgid = getpid();
		if(setpgid(dsh_pgid, dsh_pgid) < 0) {
			perror("Couldn't put the dsh in its own process group");
			exit(EXIT_FAILURE);
		}
		seize_tty(dsh_pgid);
	} 
}

/* Prints the jobs in the list.  */
void print_job(job_t *first_job) 
{
	job_t *j;
	process_t *p;
	for(j = first_job; j; j = j->next) {
        	fprintf(stdout, "\n#DISPLAY JOB INFO BEGIN#\njob: %ld, %s\n", (long)j->pgid, j->commandinfo);
		for(p = j->first_process; p; p = p->next) {
			fprintf(stdout,"cmd: %s\t", p->argv[0]);
			int i;
			for(i = 1; i < p->argc; i++) {
				fprintf(stdout, "%s ", p->argv[i]);
			}
			fprintf(stdout, "\n");
			fprintf(stdout, "Status: %d, Completed: %d, Stopped: %d\n", p->status, p->completed, p->stopped);
			fprintf(stdout, "\n");
			if(p->ifile != NULL) fprintf(stdout, "Input file name: %s\n", p->ifile);
			if(p->ofile != NULL) fprintf(stdout, "Output file name: %s\n", p->ofile);
		}
		if(j->bg) fprintf(stdout, "Background job\n");	
		else fprintf(stdout, "Foreground job\n");	
        	fprintf(stdout, "#DISPLAY JOB INFO END#\n\n");
	}
}
