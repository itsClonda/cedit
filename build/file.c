#include "stdio.h"


int main(void) {
    printf("Hello World!\n");
    printf("this fucking works!");
    char c;
    while(c = getchar()) {
        if(c == 'q') break;
         printf("%c-",c);
    }
    return 0;
}










// this comment is snake_case







// this comment stays offscreen with no lines after (printf)
