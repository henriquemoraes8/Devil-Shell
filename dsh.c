#include "dsh.h"

/* enviroment map */
extern char ** environ;

/* prompt message */
static char prompt_msg [20];

/* resume a stopped job */
void continue_job(job_t *j);

/* spawn a new job */
void spawn_job(job_t *j, bool fg);

/* Execute a program form the shell */
bool exec(process_t *p);

/* compiles code written in c or cpp usign gcc*/
void compile (process_t *p);

/* writes a log file */
void logger(const char *str, ...);

const int PIPE_READ = 0;
const int PIPE_WRITE = 1;
 
typedef int Pipe[2]; /* Defines a pipe */

job_t *job_list = NULL; /* Keep track of jobs */

job_t *get_job (int j_id); /* Returns the job corresponding to the given id */
process_t *get_process(int pid); /* Returns the process corresponding to the given id */
static void exec_nth_command(process_t *process);
void exec_pipe_command(job_t *job, process_t *process, Pipe output);
void io_redirection(process_t *process);

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
    
    if(fg && isatty(STDIN_FILENO)){// if fg is set and program has terminal
		DEBUG("exec seize with pgid %d", j->pgid);
        seize_tty(j->pgid);
        DEBUG("SEIZED");
    }// assign the terminal
    
    /* also establish child process group in child to avoid race (if parent has not done it yet). */
    set_child_pgid(j, p);
    
    /* Set the handling for job control signals back to the default. */
    signal(SIGTTOU, SIG_DFL);
    
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
    
    Pipe prev_filedes;
    
	for(p = j->first_process; p; p = p->next) {
        
        if(p->argv[0] == NULL){
            continue; //
        }
        
        Pipe next_filedes;
        
        if (pipe(next_filedes) < 0) {
            perror("ERROR: failed to create pipe");
            //TODO log message
        }
        
        switch (pid = fork()) {
            case -1: /* fork failure */
                perror("fork");
                exit(EXIT_FAILURE);
                
            case 0: /* child process  */
                p->pid = getpid();
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
                DEBUG("Child process %d detected", p -> pid);
                io_redirection(p);
                DEBUG("Executing child process");
                exec(p);
                
            default: /* parent */
                /* establish child process group */
                
                //close pipe ends
                if (p->next == NULL) {
                    close(next_filedes[PIPE_WRITE]);
                }
                close(next_filedes[PIPE_READ]);
                
                p->pid = pid;
                set_child_pgid(j, p);
                
                //The next pipe becomes the previous one
                prev_filedes[PIPE_WRITE] = next_filedes[PIPE_WRITE];
                next_filedes[PIPE_READ] = next_filedes[PIPE_READ];
                
                
                
        }
        
        /* YOUR CODE HERE?  Parent-side code for new job.*/
        int status, pid;
        DEBUG("parent waits until job %d in fg stops or terminates", j->pgid);
        pid = waitpid(WAIT_ANY, &status, WUNTRACED);

        seize_tty(getpid()); // assign the terminal back to dsh
        
    }
}

void io_redirection(process_t *process) {
    if (process -> ifile)
    {
        int fd0 = open(process -> ifile, O_RDONLY);
        if(fd0 >= 0) {
            dup2(fd0, STDIN_FILENO);
            close(fd0);
        }
        else {
            perror("Could not open file for input");
        }
    }

    if (process -> ofile)
    {
        int fd1 = creat(process -> ofile, 0644);
        if (fd1 >=0) {
            dup2(fd1, STDOUT_FILENO);
            close(fd1);
        }
        else {
            perror("Could not open file for output");
        }
    }
}

/* Given pipe, plumb it to standard output, then execute Nth command */
void exec_pipe_command(job_t *job, process_t *process, Pipe output)
{
    /* Fix stdout to write end of pipe */
    dup2(output[1], 1);
    close(output[0]);
    close(output[1]);
    if (process -> argc > 1)
    {
        pid_t pid;
        Pipe input;
        if (pipe(input) != 0)
            perror("Error: Failed to create pipe");
        if ((pid = fork()) < 0)
            perror("Error: Failed to fork");
        if (pid == 0)
        {
            /* Child */
            new_child(job, process, !(job->bg));
        }
        /* Fix standard input to read end of pipe */
        dup2(input[0], 0);
        close(input[0]);
        close(input[1]);
    }

}

/* Sends SIGCONT signal to wake up the blocked job */
void continue_job(job_t *job)
{
    process_t *main_process = get_process(job -> pgid);
    main_process -> stopped = false;
    if (kill (-job->pgid, SIGCONT) < 0) {
        perror ("kill (SIGCONT)");
        //TODO: add to log
    }
}

/* Compiles and execute a job */
bool exec(process_t *p){
    compile(p);
    printf("\n%d (Launched): %s\n", p->pid, p->argv[0]);
    if(execvp(p->argv[0], p->argv) < 0) {
            //MESSAGE TO LOGGER
        printf("%s: Command not found. \n", p->argv[0]);
        exit(EXIT_FAILURE);
        return false;
    }
    return true;
}

void compile(process_t *p){
    char *filename_p = p->argv[0];
    char *str_end_p;
    
    if((str_end_p = strstr(filename_p, ".c")) != NULL || (str_end_p = strstr(filename_p, ".cpp")) != NULL){
        int length = (int) (str_end_p - filename_p);
        if(length<=0){
            printf("Filename is empty!"); //Output to logger!
            return;
        }
        DEBUG("Filename ends with .c or .cpp\n");
        char *compiled_name = (char *) malloc(sizeof(char)*length);
        memcpy(compiled_name, filename_p, length);
        compiled_name[length] = '\0';
        DEBUG("New filename is: %s\n",compiled_name);
        
        char* c_argv[5];
        c_argv[0] = (strstr(filename_p, ".c") != NULL)? "gcc":"g++";
        c_argv[1] = "-o";
        c_argv[2] = compiled_name;
        c_argv[3] = filename_p;
        c_argv[4] = "\0";
        DEBUG("command : %s %s %s %s %s\n",
              c_argv[0],c_argv[1],c_argv[2],c_argv[3], c_argv[4]);
        job_t job;
        process_t process;
        process.next = NULL;
        process.argc = 4;
        process.argv = c_argv;
        job.next = NULL;
        job.commandinfo = NULL;
        job.bg = false;
        job.first_process=&process;
        spawn_job(&job, true);
        sprintf(p->argv[0], "./%s", compiled_name);
        free(compiled_name);
        printf("Pointer freed\n");
    }
}

/*
 * builtin_cmd - If the user has typed a built-in command then execute
 * it immediately.
 */
bool builtin_cmd(job_t *last_job, int argc, char **argv)
{
    
    /* check whether the cmd is a built in command
     */
    
    if (!strcmp(argv[0], "quit")) {
        exit(EXIT_SUCCESS);
	}
    else if (!strcmp("jobs", argv[0])) {
        
        return true;
    }
    
	else if (!strcmp("cd", argv[0])) {
        if(argc <= 1 || chdir(argv[1]) == -1) {
            //TODO: add to log
            perror("Error: invalid arguments for directory change");
        }
        return true;
    }
    
    //Background command, works as long as next argument is a reasonable id
    else if (!strcmp("bg", argv[0])) {
        int j_id = 0;
        job_t *job;
        if(argc <= 1 || !(j_id = atoi(argv[1]))) {
            //TODO: add to log
            perror("Error: invalid arguments for bg command");
            return true;
        }
        if (!(job = get_job(j_id))) {
            //TODO: add to log
            perror("Error: Could not find requested job");
            return true;
        }
        if (job -> bg == true) {
            //TODO: add to log
            perror("Error: job already in background!");
            return true;
        }
        if(job_is_completed(job)) {
            //TODO: add to log
            perror("Error: job already completed!");
            return true;
        }
        
        printf("#Sending job '%s' to background\n", job -> commandinfo);
        //TODO: add to log
        continue_job(job);
        job->bg = true;
        return true;
    }
    
    //Foreground command, works as long as next argument is a reasonable id
    else if (!strcmp("fg", argv[0])) {
        int j_id = 0;
        job_t *job;
        
        //no arguments specified, use last job
        if (argc == 1) {
            job = last_job;
        }
        //right arguments given, find respective job
        else if (argc >= 2 && (j_id = atoi(argv[1]))) {
            if (!(job = get_job(j_id))) {
                //TODO: add to log
                perror("Error: Could not find requested job");
                return true;
            }
            if (job -> bg == false) {
                //TODO: add to log
                perror("Error: job already in foreground!");
                return true;
            }
            if(job_is_completed(job)) {
                //TODO: add to log
                perror("Error: job already completed!");
                return true;
            }
        }
        else {
            //TODO: add to log
            perror("Error: invalid arguments for fg command");
            return true;
        }
        
        printf("#Sending job '%s' to foreground\n", job -> commandinfo);
        continue_job(job);
        job -> bg = false;
        seize_tty(j_id);
        //TODO make shell wait for job
        
        return true;
    }
    return false;       /* not a builtin command */
}
/* Returns the job corresponding to the given id */
job_t *get_job (int j_id) {
    job_t *job = job_list;
    while (job != NULL) {
        if (job->pgid == j_id)
            return job;
        job = job->next;
    }
    return NULL;
}

/* Returns the process corresponding to the given id */
process_t *get_process(int pid) {
    job_t *job = job_list;
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

char* promptmsg() {
    sprintf(prompt_msg, "dsh-%d$ ", (int) getpid());
	return prompt_msg;
}

void logger(const char *str, ...){
    
    
}

int main()
{
    printf("#Initializing the Devil Shell...\n");
    init_dsh(); //Comment this out in order to compile properly on gcc
    printf("#Devil Shell has started\n");
    
    
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
                spawn_job(j,!(j->bg));
            }
            j = j->next;
        }
    }
}
