#include "dsh.h"

int isspace(int c); //check whether the char c is a space

/* Initialize the members of job structure */
bool init_job(job_t *j) 
{
	j->next = NULL;
	if(!(j->commandinfo = (char *) calloc(MAX_LEN_CMDLINE,sizeof(char))))
		return false;
	j->first_process = NULL;
	j->pgid = -1; 	                /* -1 indicates spawn new job*/
	j->notified = false;
	j->mystdin = STDIN_FILENO; 	    /* 0 */
	j->mystdout = STDOUT_FILENO;	/* 1 */ 
	j->mystderr = STDERR_FILENO;	/* 2 */
	j->bg = false;
	return true;
}

/* Initialize the members of process structure */
bool init_process(process_t *p) 
{
	p->pid = -1;                    /* -1 indicates new process */
	p->completed = false;
	p->stopped = false;
	p->status = -1;                 /* set by waitpid */
	p->argc = 0;
	p->next = NULL;
	p->ifile = NULL;
	p->ofile = NULL;

	if(!(p->argv = (char **)calloc(MAX_ARGS,sizeof(char *))))
		return false;
	return true;
}

/*
 * Reads the process level information in the cases of single process or
 * cmdline with pipelines 
 *
 */

bool readprocessinfo(process_t *p, char *cmd) 
{

	int cmd_pos = 0;    /*iterator for command; */
	int args_pos = 0;   /* iterator for arguments*/

	int argc = 0;
	
	while (isspace(cmd[cmd_pos])){++cmd_pos;} /* ignore any spaces */
	if(cmd[cmd_pos] == '\0')
		return true;
	
	while(cmd[cmd_pos] != '\0'){
		if(!(p->argv[argc] = (char *)calloc(MAX_LEN_CMDLINE, sizeof(char))))
			return false;
		while(cmd[cmd_pos] != '\0' && !isspace(cmd[cmd_pos])) 
			p->argv[argc][args_pos++] = cmd[cmd_pos++];
		p->argv[argc][args_pos] = '\0';
		args_pos = 0;
		++argc;
		while (isspace(cmd[cmd_pos])){++cmd_pos;} /* ignore any spaces */
	}
	p->argv[argc] = NULL; /* required for exec_() calls */
	p->argc = argc;
	return true;
}

/* Basic parser that fills the data structures job_t and process_t defined in
 * dsh.h. We tried to make the parser flexible but it is not tested
 * with arbitrary inputs. Be prepared to hack it for the features
 * you may require. The more complicated cases such as parenthesis
 * and grouping are not supported. If the parser found some error, it
 * will always return NULL. 
 *
 * The parser supports these symbols: <, >, |, &, ;
 */

job_t* readcmdline(char *msg) 
{

	fprintf(stdout, "%s", msg);

	char *cmdline = (char *)calloc(MAX_LEN_CMDLINE, sizeof(char));
	if(!cmdline) {
	    	fprintf(stderr, "%s\n","malloc: no space");
        	return NULL;
    	}
	fgets(cmdline, MAX_LEN_CMDLINE, stdin);

	/* sequence is true only when the command line contains ; */
	bool sequence = false;
	/* seq_pos is used for storing the command line before ; */
	int seq_pos = 0;

	int cmdline_pos = 0; /*iterator for command line; */

    	job_t *first_job = NULL;

	while(1) {
		job_t *current_job = find_last_job(first_job);

		int cmd_pos = 0;        /* iterator for a command */
		int iofile_seek = 0;    /*iofile_seek for file */
		bool valid_input = true; /* check for valid input */
		bool end_of_input = false; /* check for end of input */

		/* cmdline is NOOP, i.e., just return with spaces */
		while (isspace(cmdline[cmdline_pos])){++cmdline_pos;} /* ignore any spaces */
		if(cmdline[cmdline_pos] == '\n' || cmdline[cmdline_pos] == '\0' || feof(stdin))
			return NULL;

		/* Check for invalid special symbols (characters) */
		if(cmdline[cmdline_pos] == ';' || cmdline[cmdline_pos] == '&' 
			|| cmdline[cmdline_pos] == '<' || cmdline[cmdline_pos] == '>' || cmdline[cmdline_pos] == '|')
			return NULL;

		char *cmd = (char *)calloc(MAX_LEN_CMDLINE, sizeof(char));
		if(!cmd) {
	        	fprintf(stderr, "%s\n","malloc: no space");
            		return NULL;
        	}

		job_t *newjob = (job_t *)malloc(sizeof(job_t));
		if(!newjob) {
	       		fprintf(stderr, "%s\n","malloc: no space");
            		return NULL;
        	}

		if(!first_job)
			first_job = current_job = newjob;
		else {
			current_job->next = newjob;
			current_job = current_job->next;
		}

		if(!init_job(current_job)) {
	        	fprintf(stderr, "%s\n","malloc: no space");
			delete_job(current_job,first_job);
            		return NULL;
        	}

        	process_t *newprocess = (process_t *)malloc(sizeof(process_t));
		if(!newprocess) {
	        	fprintf(stderr, "%s\n","malloc: no space");
			delete_job(current_job,first_job);
            		return NULL;
        	}
		if(!init_process(newprocess)){
	        	fprintf(stderr, "%s\n","malloc: no space");
			delete_job(current_job,first_job);
            		return NULL;
        	}

		process_t *current_process = NULL;

		if(!current_job->first_process)
			current_process = current_job->first_process = newprocess;
		else {
			current_process->next = newprocess;
			current_process = current_process->next;
		}

		while(cmdline[cmdline_pos] != '\n' && cmdline[cmdline_pos] != '\0') {

			switch (cmdline[cmdline_pos]) {

			    case '<': /* input redirection */
				current_process->ifile = (char *) calloc(MAX_LEN_FILENAME, sizeof(char));
				if(!current_process->ifile) {
					fprintf(stderr, "%s\n","malloc: no space");
					delete_job(current_job,first_job);
					return NULL;
                		}
				++cmdline_pos;
				while (isspace(cmdline[cmdline_pos])){++cmdline_pos;} /* ignore any spaces */
				iofile_seek = 0;
				while(cmdline[cmdline_pos] != '\0' && !isspace(cmdline[cmdline_pos])){
					if(MAX_LEN_FILENAME == iofile_seek) {
	                    			fprintf(stderr, "%s\n","malloc: no space");
			            		delete_job(current_job,first_job);
                        			return NULL;
                    			}
					current_process->ifile[iofile_seek++] = cmdline[cmdline_pos++];
				}
				current_process->ifile[iofile_seek] = '\0';
				current_job->mystdin = INPUT_FD;
				while(isspace(cmdline[cmdline_pos])) {
					if(cmdline[cmdline_pos] == '\n')
						break;
					++cmdline_pos;
				}
				valid_input = false;
				break;
			
			    case '>': /* output redirection */
				current_process->ofile = (char *) calloc(MAX_LEN_FILENAME, sizeof(char));
				if(!current_process->ofile) {
	                		fprintf(stderr, "%s\n","malloc: no space");
			        	delete_job(current_job,first_job);
                    			return NULL;
                		}
				++cmdline_pos;
				while (isspace(cmdline[cmdline_pos])){++cmdline_pos;} /* ignore any spaces */
				iofile_seek = 0;
				while(cmdline[cmdline_pos] != '\0' && !isspace(cmdline[cmdline_pos])){
					if(MAX_LEN_FILENAME == iofile_seek) {
	                    			fprintf(stderr, "%s\n","malloc: no space");
			            		delete_job(current_job,first_job);
                        			return NULL;
                    			}
					current_process->ofile[iofile_seek++] = cmdline[cmdline_pos++];
				}
				current_process->ofile[iofile_seek] = '\0';
				current_job->mystdout = OUTPUT_FD;
				while(isspace(cmdline[cmdline_pos])) {
					if(cmdline[cmdline_pos] == '\n')
						break;
					++cmdline_pos;
				}
				valid_input = false;
				break;

			   case '|': /* pipeline */
				cmd[cmd_pos] = '\0';
				process_t *newprocess = (process_t *)malloc(sizeof(process_t));
				if(!newprocess) {
	                		fprintf(stderr, "%s\n","malloc: no space");
			        	delete_job(current_job,first_job);
                    			return NULL;
                		}
				if(!init_process(newprocess)) {
					fprintf(stderr, "%s\n","init_process: failed");
					delete_job(current_job,first_job);
				    	return NULL;
                		}
				if(!readprocessinfo(current_process, cmd)) {
					fprintf(stderr, "%s\n","parse cmd: error");
					delete_job(current_job,first_job);
			    		return NULL;
				}
				current_process->next = newprocess;
				current_process = current_process->next;
				++cmdline_pos;
				cmd_pos = 0; /*Reinitialze for new cmd */
				valid_input = true;	
				break;

			   case '&': /* background job */
				current_job->bg = true;
				while (isspace(cmdline[cmdline_pos])){++cmdline_pos;} /* ignore any spaces */
				if(cmdline[cmdline_pos+1] != '\n' && cmdline[cmdline_pos+1] != '\0')
					fprintf(stderr, "reading bg: extra input ignored");
				end_of_input = true;
				break;

			   case ';': /* sequence of jobs*/
				sequence = true;
				strncpy(current_job->commandinfo,cmdline+seq_pos,cmdline_pos-seq_pos);
				seq_pos = cmdline_pos + 1;
				break;	

			   case '#': /* comment */
				end_of_input = true;
				break;

			   default:
				if(!valid_input) {
					fprintf(stderr, "%s\n", "reading cmdline: could not fathom input");
			        	delete_job(current_job,first_job);
                    			return NULL;
                		}
				if(cmd_pos == MAX_LEN_CMDLINE-1) {
					fprintf(stderr,"%s\n","reading cmdline: length exceeds the max limit");
			        	delete_job(current_job,first_job);
                    			return NULL;
                		}
				cmd[cmd_pos++] = cmdline[cmdline_pos++];
				break;
			}
			if(end_of_input || sequence)
				break;
		}
		cmd[cmd_pos] = '\0';
		
		if(!readprocessinfo(current_process, cmd)) {
			fprintf(stderr,"%s\n","read process info: error");
			delete_job(current_job,first_job);
            		return NULL;
        	}
		if(!sequence) {
			strncpy(current_job->commandinfo,cmdline+seq_pos,cmdline_pos-seq_pos);
			break;
		}
		sequence = false;
		++cmdline_pos;
	}
	return first_job;
}
