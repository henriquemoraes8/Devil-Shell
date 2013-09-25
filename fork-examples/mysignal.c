/* Sample program to handle signals */

#include <signal.h>
#include <stdio.h>


void * myhandler(int myint)
{
   printf("\nHey, I got signalled!!\n\n");
   exit(0);
}

int main()
{
   signal( SIGINT, (void *)  myhandler );

   while(1) {
	printf("I am in an infinite loop!\n");
	sleep(1);
   }
}
