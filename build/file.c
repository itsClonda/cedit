#include "stdio.h"
#include "string.h" 
#include "stdlib.h"

int main(int argc, char* argv[]) {
    if(argc < 1) { 
        perror("No name passed in");
        return 1; 
    } 
    char* string1 = malloc(sizeof(char)*8); 
    strcpy(string1, "I love "); 
    string1 = realloc(string1, strlen(string1)*sizeof(char) + strlen(argv[1])*sizeof(char));
    strcat(string1, argv[1]);
    printf(string1); 
	return 0;
}
