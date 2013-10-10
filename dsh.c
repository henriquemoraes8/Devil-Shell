#include "dsh.h"
#include <time.h>
#include <stdarg.h>


/* enviroment map */
extern char ** environ;

/* prompt message */
static char prompt_msg [20];

static const char *LOG_FILENAME = "dsh.log";

static const int PIPE_READ = 0;
static const int PIPE_WRITE = 1;

typedef int pipe_t[2]; /* Defines a pipe */

/* resume a stopped job */
void continue_job(job_t *j);

/* spawn a new job */
void spawn_job(job_t *j, bool fg);

/*frees a job*/
bool free_job(job_t *j);

/* Execute a program form the shell */
void exec(process_t *p);

/* compiles code written in c or cpp usign gcc*/
void compile (process_t *p);

/* writes a log file */
void logger(int fd, const char *str, ...);
/* Prints the processes running in background */
void print_jobs();

/* points to the head of a jobs linked list */
job_t *job_head = NULL;

/* points to the head of a jobs linked list */
job_t *last_job = NULL;

/*Determines whether dsh is interactive or not*/
int dsh_is_interactive;

/* finds and returns a job given a jid*/
job_t *search_job (int jid);

/* find and returns a job given an index*/
job_t *search_job_pos (int pos);

/* Returns the process corresponding to the given id */
process_t *get_process(int pid);

/*Waits for a foreground process to cease*/
void parent_wait (job_t *j, int fg);

void add_job(job_t *j);

/* Remove zombies*/
void remove_zombies();

void io_redirection(process_t *process);

void add_job(job_t *j){
    if(j!=NULL){
        if(job_head == NULL) {
            job_head = j;
        } else {
            job_t * current = job_head;
            while(current->next != NULL){
                current=current->next;
            }
            current->next = j;
        }
    }
}

/* Sets the process group id for a given job and process */
int set_child_pgid(job_t *j, process_t *p)
{
    if (j->pgid < 0) /* first child: use its pid for job pgid */
        j->pgid = p->pid;
    return(setpgid(p->pid,j->pgid));
}

/* Creates the context for a new child by setting the pid, pgid and tcsetpgrp */
void new_child(job_t *j, process_t *p, bool fg)
{
    /* establish a new process group, and put the child in
     * foreground if requested
     */
    
    /* Put the process into the process group and give the process
     * group the terminal, if appropriate.  This has to be done both by
     * the dsh and in the individual child processes because of
     * potential race conditions.
     * */
    
    p->pid = getpid();
    
    
    /* also establish child process group in child to avoid race (if parent has not done it yet). */
    set_child_pgid(j, p);
    
    if(fg && isatty(STDIN_FILENO)){// if fg is set and program has terminal
        seize_tty(j->pgid);
    }

    
    /* Set the handling for job control signals back to the default. */
    signal (SIGINT, SIG_DFL);
    /*
    signal (SIGQUIT, SIG_DFL);
    signal (SIGTSTP, SIG_DFL);
    signal (SIGTTIN, SIG_DFL);
    signal (SIGTTOU, SIG_DFL);
    signal (SIGCHLD, SIG_DFL);
*/
    
    
    /* Log errors from this child */
    int log = open(LOG_FILENAME, O_CREAT | O_WRONLY | O_APPEND);
    dup2(log, STDERR_FILENO);
    
    
}

/* Iterates through jobs and calls free on completed jobs*/
void remove_zombies() {
    job_t *job = job_head;
    job_t *prev_job;
    while (job != NULL) {
        if (job_is_completed(job)) {
            logger(STDOUT_FILENO, "Job [%d]: %s has been successfully reaped", job->pgid, job->commandinfo);
            if (job == job_head) {
                if(!free_job(job))
                   logger(STDOUT_FILENO, "Error while reaping job");
                job_head = job_head -> next;
                job = job_head;
                
            } else {
                prev_job -> next = job -> next;
                free_job(prev_job);
                job = prev_job -> next;
            }
            
        }
        else {
            prev_job = job;
            job = job -> next;
        }
    }
}

/* Spawning a process with job control. fg is true if the
 * newly-created process is to be placed in the foreground.
 * (This implicitly puts the calling process in the background,
 * so watch out for tty I/O after doing this.) pgid is -1 to
 * create a new job, in which case the returned pid is also the
 * pgid of the new job.  Else pgid specifies an existing job's
 * pgid: this feature is used to start the second or
 * subsequent processes in a pipeline.
 * */

void spawn_job(job_t *j, bool fg)
{
	pid_t pid;
	process_t *p;
    add_job(j);
    pipe_t prev_filedes;
    
	for(p = j->first_process; p; p = p->next) {
        
        if(p->argv[0] == NULL){
            continue;
        }
        
        pipe_t next_filedes;
        
        if (pipe(next_filedes) < 0) {
            logger(STDERR_FILENO, "Failed to create pipe");
                //TODO log message
        }
        
        switch (pid = fork()) {
            case -1: /* fork failure */
                logger(STDERR_FILENO,"Fork failure.");
                exit(EXIT_FAILURE);
                
            case 0: /* child process  */
                p->pid = getpid();
                
                char *msg = malloc(sizeof(p->argv[0])+20);
                sprintf(msg, "\n%d (Launched): %s\n", p->pid, p->argv[0]);
                write(STDOUT_FILENO, msg, strlen(msg));
                
                
                /* also establish child process group in child to avoid race (if parent has not done it yet). */
                set_child_pgid(j, p);

                DEBUG("%d was assigned a group %d (inside child)", p->pid, j->pgid);

                    // If it has pipe, open a write end
                if (p->next) {
                    dup2(next_filedes[PIPE_WRITE], STDOUT_FILENO);
                    close(next_filedes[PIPE_WRITE]);
                    close(next_filedes[PIPE_READ]);
                }
                    //If it is the last process, send output to stdout
                else {
                    DEBUG("last process");
                    dup2(STDOUT_FILENO, next_filedes[PIPE_WRITE]);
                }
                //If it has a previous pipe, read input
                if (p != j->first_process) {
                    dup2(prev_filedes[PIPE_READ], STDIN_FILENO);
                    close(prev_filedes[PIPE_READ]);
                    close(prev_filedes[PIPE_WRITE]);
                }
                
                new_child(j, p, fg);
                compile(p);

                DEBUG("Child process %d detected", p -> pid);
                io_redirection(p);
                exec(p);
                
                logger(STDERR_FILENO,"Failure executing child");
                exit(EXIT_FAILURE);
                
            default: /* parent */
                /* establish child process group */
                
                p->pid = pid;
                set_child_pgid(j, p);
                
                    //close pipe ends
                if (p->next == NULL) {
                    close(next_filedes[PIPE_READ]);
                }
                close(next_filedes[PIPE_WRITE]);
                
                    //The next pipe becomes the previous one
                prev_filedes[PIPE_WRITE] = next_filedes[PIPE_WRITE];
                prev_filedes[PIPE_READ] = next_filedes[PIPE_READ];
        }
        parent_wait(j, fg);
    }
}

/*Makes the parent process wait for a child to finish execution
 when the child is on the foreground*/
void parent_wait (job_t *j, int fg) {
    if(fg){
        DEBUG("parent is waiting for child");
        int status, pid;
        while((pid = waitpid(WAIT_ANY, &status, WUNTRACED)) > 0){
            process_t *p = get_process(pid);
            if (WIFEXITED(status)){
                p->completed = true;
                if (status == EXIT_SUCCESS) {
                    printf("%d (Completed): %s\n", pid, p->argv[0]);
                }
                else {
                    printf("%d (Failed): %s\n", pid, p->argv[0]);
                }
                fflush(stdout);
            }
            else if (WIFSTOPPED(status)) {
                DEBUG("Process %d stopped", p->pid);
                if (kill (-j->pgid, SIGSTOP) < 0) {
                    logger(STDERR_FILENO,"Kill (SIGSTOP) failed.");
                }
                p->stopped = 1;
                j->notified = true;
                j->bg = true;
                print_jobs();
                break;
            }
            
            else if (WIFCONTINUED(status)) { DEBUG("Process %d resumed", p->pid); p->stopped = 0; }
            else if (WIFSIGNALED(status)) { DEBUG("Process %d terminated", p->pid); p->completed = 1; }
            else logger(2, "Child %d terminated abnormally", pid);
            if (job_is_stopped(j) && isatty(STDIN_FILENO)) {
                seize_tty(getpid());
                break;
            }
        }
    }
}

void io_redirection(process_t *process){
    if (process -> ifile) {
        int fd0 = open(process -> ifile, O_RDONLY);
        if(fd0 >= 0) {
            dup2(fd0, STDIN_FILENO);
            close(fd0);
        }
        else {
            logger(STDERR_FILENO,"Could not open file for input");
        }
    }
    
    if (process -> ofile) {
        int fd1 = creat(process -> ofile, 0644);
        if (fd1 >=0) {
            dup2(fd1, STDOUT_FILENO);
            close(fd1);
        }
        else {
            logger(STDERR_FILENO,"Could not open file for output");
        }
    }
    
}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *job){
    process_t *main_process = get_process(job -> pgid);
    main_process -> stopped = false;
    if (kill (-job->pgid, SIGCONT) < 0) {
        logger(STDERR_FILENO,"Kill (SIGCONT)");
    }
}

/* Compiles and execute a job */
void exec(process_t *p){
    if(execvp(p->argv[0], p->argv) < 0) {
        logger(STDERR_FILENO, "%s: Command not found.", p->argv[0]);
        exit(EXIT_FAILURE);
    }
}

void compile(process_t *p){
    char *filename_p = p->argv[0];
    char *str_end_p;
    
    if((str_end_p = strstr(filename_p, ".c")) != NULL || (str_end_p = strstr(filename_p, ".cpp")) != NULL){
        printf("Compiling...");
        int length = (int) (str_end_p - filename_p);
        if(length<=0){
            logger(STDERR_FILENO, "Filename is not valid."); //Output to logger!
            return;
        }
        DEBUG("Filename ends with .c or .cpp\n");
        char *compiled_name = (char *) malloc(sizeof(char)*length);
        memcpy(compiled_name, filename_p, length);
        compiled_name[length] = '\0';
        DEBUG("New filename is: %s\n",compiled_name);
        
        
        char **c_argv = (char **) malloc(sizeof(char *)*5);
        c_argv[0] = (strstr(filename_p, ".c") != NULL)? "gcc":"g++";
        c_argv[1] = "-o";
        c_argv[2] = compiled_name;
        c_argv[3] = filename_p;
        c_argv[4] = "\0";
        DEBUG("command : %s %s %s %s\n",
              c_argv[0],c_argv[1],c_argv[2],c_argv[3]);
        
        int status;
        pid_t pid;
        
        //FORKEXEC
        switch (pid = fork()) {
            case -1: logger(2, "Fork failure at compiler");
            case 0:
                execv("/usr/bin/gcc", c_argv);
            default:
                waitpid(pid, &status, WUNTRACED);
                 if (WIFEXITED(status)){
                     printf("status: %d", status);
                 }
        }
        sprintf(p->argv[0], "./%s", compiled_name);
        free(compiled_name);
        free(c_argv);


        DEBUG("Pointers freed up!");
    }
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv){
    
    /* check whether the cmd is a built in command
     */
    
    if (!strcmp(argv[0], "quit")) {
        exit(EXIT_SUCCESS);
	}
    else if (!strcmp("jobs", argv[0])) {
        print_jobs();
        return true;
    }
	else if (!strcmp("cd", argv[0])) {
        if(argc <= 1 || chdir(argv[1]) == -1) {
            logger(STDERR_FILENO,"Error: invalid arguments for directory change");
        }
        return true;
    }
    
        //Background command, works as long as next argument is a reasonable id
    else if (!strcmp("bg", argv[0])) {
        int jid = 0;
        job_t *job;
        if(argc <= 2 || !(jid = atoi(argv[1]))) {
            logger(STDERR_FILENO,"Error: invalid arguments for bg command");
            return true;
        }
        if (!(job = search_job(jid))) {
            logger(STDERR_FILENO, "Error: Could not find requested job");
            return true;
        }
        if (job -> bg == true) {
            logger(STDERR_FILENO, "Error: job already in background!");
            return true;
        }
        if(job_is_completed(job)) {
            logger(STDERR_FILENO, "Error: job is already completed!");
            return true;
        }
        
        printf("#Sending job '%s' to background\n", job -> commandinfo);
        fflush(stdout);
        continue_job(job);
        job->bg = true;
        job->notified = false;
        return true;
    }
    
        //Foreground command, works as long as next argument is a reasonable id
    else if (!strcmp("fg", argv[0])) {
        int pos = 0;
        job_t *job;
        
            //no arguments specified, use last job
        if (argc == 1) {
            job = search_job_pos(-1);
        }
            //right arguments given, find respective job
        else if (argc >= 2 && (pos = atoi(argv[1]))) {
            if (!(job = search_job_pos(pos))) {
                logger(STDERR_FILENO, "Could not find requested job");
                return true;
            }
            if (job -> bg == false) {
                logger(STDERR_FILENO, "The job is already in foreground.");
                return true;
            }
            if(job_is_completed(job)) {
                logger(STDERR_FILENO,"Job already completed!");
                return true;
            }
        }
        else {
            logger(STDERR_FILENO,"Invalid arguments for fg command");
            return true;
        }
        
        
        printf("#Sending job '%s' to foreground\n", job -> commandinfo);
        fflush(stdout);
        continue_job(job);
        job -> bg = false;
        seize_tty(job->pgid);
        parent_wait(job, true);
        return true;
    }
    return false;       /* not a builtin command */
}
/* Returns the job corresponding to the given id */
job_t *search_job (int jid) {
    job_t *job = job_head;
    while (job != NULL) {
        if (job->pgid == jid)
            return job;
        job = job->next;
    }
    return NULL;
}
job_t *search_job_pos (int pos){
    job_t *job = job_head;
    int count = pos;
    while (job != NULL) {
        if(count == 1){
            return job;
        }
        if(job->next == NULL){
            return job;
        }
        count--;
        job = job->next;
    }
    return NULL;
}

/* Returns the process corresponding to the given id */
process_t *get_process(int pid) {
    job_t *job = job_head;
    while (job != NULL) {
        process_t *process = job -> first_process;
        while (process != NULL){
            if (process -> pid == pid)
                return process;
            process = process ->next;
        }
        job = job -> next;
    }
    return NULL;
}

char* promptmsg(){
    if (!dsh_is_interactive) return "";
    sprintf(prompt_msg, "dsh-%d$ ", (int) getpid());
	return prompt_msg;
}

void print_jobs(){
    int count = 1;
    remove_zombies();
    job_t *j = job_head;
    if (j == NULL) {
        char *msg = "No jobs are running\n";
        write(STDOUT_FILENO, msg, strlen(msg));
        return;
    }
    while(j!=NULL){
        printf("[%d]", count);
        if(j->bg)
            printf(" bg ");
        else
            printf(" fg ");
        if(j->notified)
            printf(" Stopped        ");
        else
            printf(" Running        ");
        printf("%s\n", j->commandinfo);
        j = j->next;
        count++;
    }
    fflush(stdout);
}
void logger(int fd, const char *str, ...){
    va_list argptr;
    
    FILE *file;
    
    file=fopen(LOG_FILENAME,"a");
    
    if (file!=NULL){
        time_t ltime; /* calendar time */
        ltime = time(NULL); /* get current cal time */
        char time[20];
        strftime (time,80,"%c",localtime(&ltime));
        
        if(fd == 2){
            fprintf(file, "[%s] ERROR: ", time);
            printf("[%s] ERROR: ", time);
        } else {
            fprintf(file, "[%s]: ", time);
            printf("[%s]: ", time);
        }
        va_start(argptr, str);
        vfprintf(file, str, argptr);
        va_end(argptr);
        va_start(argptr, str);
        vprintf(str, argptr);
        va_end(argptr);
        fprintf(file, "\n");
        printf("\n");
        fclose(file);
        fflush(stdout);
    }
}

int main(){
    printf("#Initializing the Devil Shell...\n");
    init_dsh(); //Comment this out in order to compile properly on gcc
    printf("#Devil Shell has started\n");
    job_head = NULL;
	while(1) {
        job_t *j = NULL;
        if(!(j = readcmdline(promptmsg()))) {
			if (feof(stdin)) { /* End of file (ctrl-d) */
				fflush(stdout);
				printf("\n");
				exit(EXIT_SUCCESS);
            }
			continue; /* NOOP; user entered return or spaces with return */
		}
        /* Only for debugging purposes to show parser output; turn off in the
         * final code */
        if(PRINT_INFO) print_job(j);
        /* Your code goes here */
        /* You need to loop through jobs list since a command line can contain ;*/
        while(j!= NULL){
            /* Check for built-in commands */
            int argc = j->first_process->argc;
            char **argv = j->first_process->argv;
            if(!builtin_cmd(j, argc, argv)){
                DEBUG("***going to spawn job***");
               
                process_t *p = j->first_process;
                
                while(p!=NULL){
                    compile(p);
                    p=p->next;
                }
                
                spawn_job(j,!(j->bg));
            }
            j = j->next;
            
        }
        
    }
}
