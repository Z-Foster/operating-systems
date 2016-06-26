/* CSci4061
* section: 5
* date: 03/21/16
* name: Zach Foster
* id: foste448 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

/*
 * Read a line from stdin.
 */
char* sh_read_line(void)
{
    char* line = NULL;
    ssize_t bufsize = 0;

    getline(&line, &bufsize, stdin);
    return line;
}

/*
 * Do stuff with the shell input here.
 */
int sh_handle_input(char* line, int fd_toserver, bool is_server)
{
    // Cause segfault if segfault command was given.
    if (starts_with(line, CMD_SEG) && !is_server) {
        char* n = NULL;
        *n = 1;
    }
    // Otherwise write message to server for processing.
    if (write(fd_toserver, line, strlen(line) + 1) == -1) {
        perror("Error writing to server.\n");
    }
    return 0;
}

/*
 * Check if the line is empty (no data; just spaces or return)
 */
int is_empty(char* line)
{
    while (*line != '\0') {
        if (!isspace(*line))
            return 0;
        line++;
    }
    return 1;
}

/*
 * Start the main shell loop:
 * Print prompt, read user input, handle it.
 */
void sh_start(char* name, int fd_toserver, bool is_server)
{
    print_prompt(name);
    char message[MSG_SIZE];
    while (true) {
        strcpy(message, sh_read_line());
        sh_handle_input(message, fd_toserver, is_server);
        print_prompt(name);
    }
}

int main(int argc, char** argv)
{
    bool is_server = false;
    user_chat_box_t shell_box;
    pid_t childpid;
    char msg_from_server[MSG_SIZE];
    int readsize;
    char szchild_pid[100];

    /* Extract pipe descriptors and name from argv */
    // If user chat, define user chat box. Otherwise define server controller.
    if (argc == 6) {
        strcpy(shell_box.name, argv[1]);
        shell_box.ptoc[0] = atoi(argv[2]);
        shell_box.ptoc[1] = atoi(argv[3]);
        shell_box.ctop[0] = atoi(argv[4]);
        shell_box.ctop[1] = atoi(argv[5]);
    } else if (argc == 5) {
        strcpy(shell_box.name, "Server");
        shell_box.ptoc[0] = atoi(argv[1]);
        shell_box.ptoc[1] = atoi(argv[2]);
        shell_box.ctop[0] = atoi(argv[3]);
        shell_box.ctop[1] = atoi(argv[4]);
        is_server = true;
    } else {
        printf("Shell called with invalid number of arguments.%d\n", argc);
        return -1;
    }

    /* Fork a child to read from the pipe continuously */
    childpid = fork();

    if (childpid == 0) {
        // Close appropriate pipe ends.
        if ((close(shell_box.ptoc[1])) == -1) {
            perror("Failed to close write end of parent to child pipe.\n");
        }
        if ((close(shell_box.ctop[0])) == -1) {
            perror("Failed to close read end of child to parent pipe.\n");
        }
        if ((close(shell_box.ctop[1])) == -1) {
            perror("Failed to close write end of child to parent pipe.\n");
        }
        while (true) {
            // Look for new data from server every 1000 usecs and print it
            usleep(1000);
            if ((readsize = read(shell_box.ptoc[0], msg_from_server, MSG_SIZE)) > 0) {
                if (readsize == -1) {
                    // continue;
                    // perror("Read error in shell (from server).\n");
                } else {
                    printf("%s\n", msg_from_server);
                    fflush(stdout);
                }
            }
        }
    } else if (childpid > 0) {
        // Close appropriate pipe ends.
        if ((close(shell_box.ptoc[0])) == -1) {
            perror("Failed to close read end of parent to child pipe.\n");
        }
        if ((close(shell_box.ptoc[1])) == -1) {
            perror("Failed to close read end of parent to child pipe.\n");
        }
        if ((close(shell_box.ctop[0])) == -1) {
            perror("Failed to close write end of child to parent pipe.\n");
        }

        sprintf(szchild_pid, "\\child_pid %d", childpid);
        // Send the child's pid to the server for later cleanup
        if ((write(shell_box.ctop[1], szchild_pid, 100)) == -1) {
            perror("Error writing childpid to server");
            kill(childpid,SIGKILL);
            return -1;
        }
        // Start the main shell loop
        sh_start(shell_box.name, shell_box.ctop[1], is_server);
    } else {
        perror("fork error");
        return -1;
    }
}
