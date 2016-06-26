/* CSci4061
* section: 5
* date: 03/21/16
* name: Zach Foster
* id: foste448 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>

#include "util.h"

/**
 * Trim command from message from shell.
 */
char* trim_command(char* buf){

    char msg[MSG_SIZE];
    char *s;

    sprintf(msg, "%s", buf);
    s = strtok(msg, " ");
    s = strtok(NULL, " ");
    // s = strtok(NULL, "\n");
    return s;
}

/*
 * Identify the command used at the shell
 */
int parse_command(char* buf)
{
    int cmd;

    if (starts_with(buf, CMD_CHILD_PID))
        cmd = CHILD_PID;
    else if (starts_with(buf, CMD_P2P))
        cmd = P2P;
    else if (starts_with(buf, CMD_LIST_USERS))
        cmd = LIST_USERS;
    else if (starts_with(buf, CMD_ADD_USER))
        cmd = ADD_USER;
    else if (starts_with(buf, CMD_EXIT))
        cmd = EXIT;
    else if (starts_with(buf, CMD_KICK))
        cmd = KICK;
    else
        cmd = BROADCAST;

    return cmd;
}

/*
 * List the existing users on the server shell
 */
int list_users(user_chat_box_t* users, int fd)
{
    int i;
    int empty = 0;
    char username_list[MSG_SIZE*MAX_USERS];
    strcpy(username_list, "\n");
    
    for(i = 0; i < MAX_USERS; ++i){
        if(users[i].status == SLOT_FULL){
            strcat(username_list, "\n");
            strcat(username_list, users[i].name);
        } else {
            empty++;
            if(empty == 10){
                strcpy(username_list, "<no users>");
            }
        }
    }
    if(write(fd, username_list, MSG_SIZE) < 0) {
        perror("write to child shell during list_user failed");
    }
    return 0;
}

/*
 * Add a new user
 */
int add_user(user_chat_box_t* users, char* buf){

    int i, j;
    int full = 0;
    char name[MSG_SIZE];
    char* s;
    // Iterate through users array. Keep track of number of users present so
    // we can print error message when the chatroom is full.
    for (i = 0; i < MAX_USERS; ++i) {
        if (users[i].status == SLOT_FULL) {
            ++full;
            if(full == MAX_USERS){
                printf("Max user limit reached\n");
                perror("could not add user");
                return -1;
            }
            continue;
        }
        //Store name in user_chat_box struct and change status to full
        sprintf(name, "%s", buf);
        s = strtok(name, " \n");
        s = strtok(NULL, " \n");
        strcpy(users[i].name, s);
        printf("\nAdding user %s...\n", s);
        users[i].status = SLOT_FULL;
        ++full;

        /* 
         * If limit is okay, add user, set up non-blocking pipes and
         * notify on server shell
         */

        //Open up bidirectional non-blocking pipes
        pid_t user_shell_pid;
        int serv_to_user_shell[2];
        int user_shell_to_serv[2];
        int s2us0_flags;
        int s2us1_flags;
        int us2s0_flags;
        int us2s1_flags;

        int s2us0;
        int s2us1;
        int us2s0;
        int us2s1;

        char szs2us0[33];
        char szs2us1[33];
        char szus2s0[33];
        char szus2s1[33];

        if ((pipe(serv_to_user_shell)) == -1) {
            perror("Error opening server to server user shell pipe.\n");
            return -1;
        }
        if ((pipe(user_shell_to_serv)) == -1) {
            perror("Error opening user shell to server pipe.\n");
            return -1;
        }

        s2us0 = serv_to_user_shell[0];
        s2us1 = serv_to_user_shell[1];
        us2s0 = user_shell_to_serv[0];
        us2s1 = user_shell_to_serv[1];

        sprintf(szs2us0, "%d", s2us0);
        sprintf(szs2us1, "%d", s2us1);
        sprintf(szus2s0, "%d", us2s0);
        sprintf(szus2s1, "%d", us2s1);

        s2us0_flags = fcntl(s2us0, F_GETFL, 0);
        s2us1_flags = fcntl(s2us1, F_GETFL, 0);
        us2s0_flags = fcntl(us2s0, F_GETFL, 0);
        us2s1_flags = fcntl(us2s1, F_GETFL, 0);

        if (fcntl(s2us0, F_SETFL, s2us0_flags | O_NONBLOCK) == -1) {
            perror("Failed to set read end of server to user shell pipe to nonblocking\n");
            return -1;
        }
        if (fcntl(s2us1, F_SETFL, s2us1_flags | O_NONBLOCK) == -1) {
            perror("Failed to set read end of server to user shell pipe to nonblocking\n");
            return -1;
        }
        if (fcntl(us2s0, F_SETFL, us2s0_flags | O_NONBLOCK) == -1) {
            perror("Failed to set read end of user shell to server pipe to nonblocking\n");
            return -1;
        }
        if (fcntl(us2s1, F_SETFL, us2s1_flags | O_NONBLOCK) == -1) {
            perror("Failed to set read end of user shell to server pipe to nonblocking\n");
            return -1;
        }

        //Store pipes in user_chat_box struct
        users[i].ptoc[0] = s2us0;
        users[i].ptoc[1] = s2us1;
        users[i].ctop[0] = us2s0;
        users[i].ctop[1] = us2s1;

        //fork the user shell
        user_shell_pid = fork();
        if (user_shell_pid < 0) {
            perror("forking of user shell failed");
            return -1;
        } else if (user_shell_pid == 0) {
            // We don't close pipe ends here, close them in shell.c.
            // Exec xterm with user's shell program.
            execl("/usr/bin/xterm",XTERM,"+hold","-e",
                "./shell", users[i].name, szs2us0,
                szs2us1, szus2s0, szus2s1, (char *) NULL);
            perror("execl failed while adding user");
        } else {
            // Store user_shell_pid in the user_chat_box struct
            users[i].pid = user_shell_pid;
            // Close unused pipe ends.
            if (close(s2us0) == -1) {
                perror("closing pipe end failed");
                return -1;
            }
            if (close(us2s1) == -1) {
                perror("closing pipe end failed");
                return -1;
            }
        }
        break;
    }
    return 0;
}

/*
 * Broadcast message to all users. Completed for you as a guide to help with
 * other commands :-).
 */
int broadcast_msg(user_chat_box_t* users, char* buf, int fd, char* sender)
{
    int i;
    const char *msg = "Broadcasting...", *s;
    char text[MSG_SIZE];

    /* Notify on server shell */
    if (write(fd, msg, strlen(msg) + 1) < 0)
        perror("writing to server shell");

    /* Send the message to all user shells */
    s = strtok(buf, "\n");
    sprintf(text, "%s : %s", sender, s);
    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_EMPTY)
            continue;
        if (write(users[i].ptoc[1], text, strlen(text) + 1) < 0){
            perror("write to child shell failed");
            return -1;
        }
    }
    return 0;
}

/*
 * Close all pipes for this user
 */
void close_pipes(int idx, user_chat_box_t* users)
{
    // Close all pipes, ignore error messages because they may already be closed.
    close(users[idx].ctop[0]);
    close(users[idx].ptoc[1]);
    close(users[idx].ctop[1]);
    close(users[idx].ptoc[0]);
}

/*
 * Cleanup single user: close all pipes, kill user's child process, kill user
 * xterm process, free-up slot.
 * Remember to wait for the appropriate processes here!
 */
void cleanup_user(int idx, user_chat_box_t* users)
{   
    int status;
    close_pipes(idx, users);
    strcpy(users[idx].name, "");
    users[idx].status = SLOT_EMPTY;

    // Ignore error messages because system may have killed the process
    // in the case of seg fault.
    kill(users[idx].child_pid, SIGKILL);
    kill(users[idx].pid, SIGKILL);

    // Wait for zombies.
    waitpid(users[idx].child_pid, &status, 0);
    waitpid(users[idx].pid, &status, 0);
}

/*
 * Cleanup all users: given to you
 */
void cleanup_users(user_chat_box_t* users)
{
    int i;

    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_FULL)
            cleanup_user(i, users);
    }
}

/*
 * Cleanup server process: close all pipes, kill the parent process and its
 * children.
 * Remember to wait for the appropriate processes here!
 */
void cleanup_server(server_ctrl_t server_ctrl, bool processes)
{
    int status;

    if (close(server_ctrl.ctop[0]) == -1)
        perror("Closing read pipe from shell to server failed");
    if (close(server_ctrl.ptoc[1]) == -1)
        perror("Closing write pipe from server to it's shell failed.");

    // Do not check for errors for these, they should have already been closed.
    close(server_ctrl.ctop[1]);
    close(server_ctrl.ptoc[0]);
    // Only kill and wait for zombies if child processes have been created.
    if(processes) {
        kill(server_ctrl.child_pid, SIGKILL);
        kill(server_ctrl.pid, SIGKILL);

        // Wait for zombies.
        waitpid(server_ctrl.child_pid, &status, 0);
        waitpid(server_ctrl.pid, &status, 0);
    }
}

/*
 * Utility function.
 * Find user index for given user name.
 */
int find_user_index(user_chat_box_t* users, char* name)
{
    int i, user_idx = -1;

    if (name == NULL) {
        fprintf(stderr, "NULL name passed.\n");
        return user_idx;
    }
    for (i = 0; i < MAX_USERS; i++) {
        if (users[i].status == SLOT_EMPTY)
            continue;
        if (strcmp(users[i].name, name) == 0) {
            user_idx = i;
            break;
        }
    }
    return user_idx;
}

/*
 * Utility function.
 * Given a command's input buffer, extract name.
 */
char* extract_name(int cmd, char* buf)
{
    char* s = NULL;

    s = strtok(buf, " ");
    s = strtok(NULL, " ");
    if (cmd == P2P)
        return s;        /* s points to the name as no newline after name in P2P */
    s = strtok(s, "\n"); /* other commands have newline after name, so remove it*/
    return s;
}

/*
 * Send personal message. Print error on the user shell if user not found.
 */
void send_p2p_msg(int idx, user_chat_box_t* users, char* buf)
{
    int i = -1;
    char text[MSG_SIZE];
    char rcvname[MSG_SIZE];
    char* s;
    char* buf2 = strdup(buf);
    char buffer[MSG_SIZE];
    /* extract username and get its index with find_user_index*/
    strcpy(rcvname, extract_name(P2P, buf));
    i = find_user_index(users, rcvname);
    if(i != -1){
        /*Extract msg and and send it to user via pipe*/
        s = strtok(buf2, " ");;
        s = strtok(NULL, " ");
        s = strtok(NULL, "\n");
        sprintf(text, "%s : %s", users[idx].name, s);
        if (write(users[i].ptoc[1], text, strlen(text) + 1) < 0)
           perror("write to child shell failed");
    } else {
        // If user wasn't found, display error message to sender.
        strcpy(text, "User not found");
        if (write(users[idx].ptoc[1], text, strlen(text) + 1) < 0){
            perror("write to child shell failed");
        }
    }
}

void init_users(user_chat_box_t* users){
    int i;
    for(i = 0; i < MAX_USERS; ++i){
        users[i].status = SLOT_EMPTY;
    }
}

int main(int argc, char** argv)
{
    int i;

    pid_t server_shell_pid;
    pid_t child_pid;
    int status;

    server_ctrl_t server;
    user_chat_box_t users[MAX_USERS];

    char message_from_shell[MSG_SIZE];
    int shell_command, user_idx, read_val;

    int serv_to_serv_shell[2];
    int serv_shell_to_serv[2];
    
    char s2ss0[33];
    char s2ss1[33];
    char ss2s0[33];
    char ss2s1[33];

    int s2ss0_flags;
    int s2ss1_flags;
    int ss2s0_flags;
    int ss2s1_flags;

    /* Initialize array of users*/
    init_users(users);

    /* open non-blocking bi-directional pipes for communication with server shell
    */
    if ((pipe(serv_to_serv_shell)) == -1) {
        perror("Error opening server to server shell pipe.\n");
        return -1;
    }
    if ((pipe(serv_shell_to_serv)) == -1) {
        perror("Error opening server shell to server pipe.\n");
        close(serv_to_serv_shell[0]);
        close(serv_to_serv_shell[1]);
        return -1;
    }

    server.ptoc[0] = serv_to_serv_shell[0];
    server.ptoc[1] = serv_to_serv_shell[1];
    server.ctop[0] = serv_shell_to_serv[0];
    server.ctop[1] = serv_shell_to_serv[1];

    sprintf(s2ss0, "%d", serv_to_serv_shell[0]);
    sprintf(s2ss1, "%d", serv_to_serv_shell[1]);
    sprintf(ss2s0, "%d", serv_shell_to_serv[0]);
    sprintf(ss2s1, "%d", serv_shell_to_serv[1]);

    s2ss0_flags = fcntl(serv_to_serv_shell[0], F_GETFL, 0);
    s2ss1_flags = fcntl(serv_to_serv_shell[1], F_GETFL, 0);
    ss2s0_flags = fcntl(serv_shell_to_serv[0], F_GETFL, 0);
    ss2s1_flags = fcntl(serv_shell_to_serv[1], F_GETFL, 0);

    if (fcntl(serv_to_serv_shell[0], F_SETFL, s2ss0_flags | O_NONBLOCK) == -1) {
        perror(
            "Failed to set read end of server to server shell pipe to "
            "nonblocking\n");
        cleanup_server(server, false);
        return -1;
    }
    if (fcntl(serv_to_serv_shell[1], F_SETFL, s2ss1_flags | O_NONBLOCK) == -1) {
        perror(
            "Failed to set read end of server to server shell pipe to "
            "nonblocking\n");
        cleanup_server(server, false);
        return -1;
    }
    if (fcntl(serv_shell_to_serv[0], F_SETFL, ss2s0_flags | O_NONBLOCK) == -1) {
        perror(
            "Failed to set read end of server shell to server pipe to "
            "nonblocking\n");
        cleanup_server(server, false);
        return -1;
    }
    if (fcntl(serv_shell_to_serv[1], F_SETFL, ss2s1_flags | O_NONBLOCK) == -1) {
        perror(
            "Failed to set read end of server shell to server pipe to "
            "nonblocking\n");
        cleanup_server(server, false);
        return -1;
    }

    /* Fork the server shell */
    server_shell_pid = fork();
    if (server_shell_pid == 0) {
        // Start server's shell in child w/ exec.
        // Correct pipes will be closed in shell.c.
        execl("./shell", SHELL_PROG, s2ss0, s2ss1, ss2s0, ss2s1,
            (char*)NULL);
    } else if (server_shell_pid > 0) {
        server.pid = server_shell_pid;
        // Close correct pipes.
        if (close(server.ptoc[0]) == -1) {
            perror("Closing read pipe from server to server shell failed.");
            cleanup_server(server, true);
            return -1;
        }
        if (close(server.ctop[1]) == -1) {
            perror("Closing write pipe from server shell to server failed");
            cleanup_server(server, true);
            return -1;
        }
        /* Inside the parent. This will be the most important part of this program.
        */
        /* Start a loop which runs every 1000 usecs.
        * The loop should read messages from the server shell, parse them using the
        * parse_command() function and take the appropriate actions. */
        while (1) {
            /* Let the CPU breathe */
            usleep(1000);
            if ((read(serv_shell_to_serv[0], message_from_shell, MSG_SIZE)) > 0) {
                // Respond correctly to the command entered in shell.
                switch (parse_command(message_from_shell)) {
                case CHILD_PID:
                    server.child_pid = atoi(trim_command(message_from_shell));
                    break;
                case LIST_USERS:
                    list_users(users, server.ptoc[1]);
                    break;
                case ADD_USER:
                    add_user(users, message_from_shell);
                    break;
                case EXIT:
                    cleanup_users(users);
                    cleanup_server(server, true);
                    // After cleaning up users and server, exit program.
                    return 0;
                    break;
                case BROADCAST:
                    if(strcmp("\n", message_from_shell) != 0) {
                        broadcast_msg(users, message_from_shell,
                            server.ptoc[1], "Server");
                    }
                    break;
                case KICK:
                    if((user_idx = find_user_index(users, extract_name(KICK, message_from_shell))) != -1) {
                        cleanup_user(user_idx, users);
                    } else {
                        printf("User not found.\n");
                    }
                    break;
                }
            }

            for (i = 0; i < MAX_USERS; i++) {
                if (users[i].status == SLOT_FULL){
                    if ((read_val = read(users[i].ctop[0], message_from_shell, MSG_SIZE)) > 0) {
                        // Respond correctly to the command entered in shell.
                        switch (parse_command(message_from_shell)) {
                        case CHILD_PID:
                            users[i].child_pid = atoi(trim_command(message_from_shell));
                            break;
                        case LIST_USERS:
                            list_users(users, users[i].ptoc[1]);
                            break;
                        case P2P:
                            send_p2p_msg(i, users, message_from_shell);
                            break;
                        case EXIT:
                            cleanup_user(i, users);
                            break;
                        case BROADCAST:
                            // Check if message is empty. If it is, ignore it.
                            if(strcmp("\n", message_from_shell) != 0) {
                                broadcast_msg(users, message_from_shell,
                                    server.ptoc[1], users[i].name);
                            }
                            break;
                        }
                    }
                    if(waitpid(users[i].pid, &status, WNOHANG) > 0){
                        cleanup_user(i, users);
                    }
                }
            }
        }
    }
    return 0;
}
