CSci4061 Project 2

name: Zach Foster

Tested on x86_64 Linux lab machine.

# "Local" Multi-Party chat using IPC
The purpose of this program is to simulate a chat application using processes and interprocess communication (IPC). The main process of the application is the server process. The server process then launches a shell process for the administrator to interact with. The server process may also launch shell processes for users.

## Compilation
While in the application's root folder:

$ make clean

$ make

## Execution
While in the applications root folder enter the following to start the application.

$ ./server

## Commands
### Administrator Only Commands
$ \add <user_name>
The add command creates a new shell process for a user with the specified user name.

$ \kick <user_name>
The kick command kicks a user 

### User Only Commands
$ \p2p <user_name> <message>
The p2p (peer-to-peer) command acts as a private message to the user with the specified username.

$ \seg
The seg command causes a segmentation fault in the user's shell process. The server process is responsible for gracefully handling the segmentation fault.

### Administrator and User Commands
$ <message>
Sends the message to all users.

$ \list
The list command lists the currently "online" users.

$ \exit
If called by a user, the user's process is terminated. If called by the administrator, all user's processes are terminated, then the administrator shell process is terminated. Finally the server process terminates.
