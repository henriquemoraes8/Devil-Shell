/* Sample infinite loop program */

#include <stdio.h>

int main()
{
    int count = 0;
   while(1) {
    printf("counter is: %d\n", count);
	sleep(1);
    count ++;
   }
}
