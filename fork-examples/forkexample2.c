#include <stdio.h>
#include <unistd.h> //fork()
#include <stdlib.h> //exit()
#include <string.h> //strcpy

int gVar = 2;

int main() {
	char pName[80] = "";
	int lVar = 20;
	
	pid_t pID = fork();
	if(pID == 0) {	//child
		// Code only executed by child process
		strcpy(pName,"Child Process: ");	
		++gVar;
		++lVar;	
	}
	else if(pID < 0) { //failed to fork
		printf("Failed to fork\n");
		exit(1); 
	}
	else {		//parent
		//Code only executed by parent process
		strcpy(pName,"Parent Process: ");
	}
	
	//Code executed by both parent and child process
	printf("%s",pName);
	printf("Global Variabile: %d ",gVar);
	printf("Local Variable: %d\n",lVar);
}
