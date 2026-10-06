# NetMessenger — TCP Client-Server Chat Application

## Project Overview

NetMessenger is a multi-client TCP chat application developed in the C programming language.

The application uses a client-server architecture where multiple clients can connect to a central server and communicate using a custom text-based protocol.

The project demonstrates fundamental computer systems and networking concepts including TCP sockets, concurrent client handling, message protocols, file transfer, synchronization, and error handling.

## Features

* TCP client-server communication
* Multiple simultaneous clients
* User registration
* Connected-user listing
* Broadcast messaging
* Private messaging
* Chat rooms
* Joining and leaving rooms
* Room-based messaging
* File transfer between clients
* Graceful client disconnection
* Unexpected client disconnect handling
* Invalid-command and error handling
* Multithreaded server using POSIX threads
* Thread synchronization using mutexes

## Supported Commands

| Command                          | Description                 |
| -------------------------------- | --------------------------- |
| `REGISTER <username>`            | Register a username         |
| `LIST`                           | Display connected users     |
| `BCAST <message>`                | Broadcast a message         |
| `PMSG <username> <message>`      | Send a private message      |
| `JOIN <room>`                    | Join a chat room            |
| `LEAVE <room>`                   | Leave a chat room           |
| `ROOMS`                          | Display available rooms     |
| `RMSG <room> <message>`          | Send a message to a room    |
| `SENDFILE <username> <filename>` | Send a file to another user |
| `QUIT`                           | Disconnect from the server  |

## Technologies

* C
* TCP/IP
* POSIX Sockets
* POSIX Threads (`pthread`)
* Linux / CentOS
* GCC
* Git

## Project Structure

```text
IE3010-NetMessenger/
├── client_3744.c
├── server_3744.c
├── .gitignore
└── README.md
```

The compiled binaries are intentionally excluded from Git using `.gitignore`.

## Compilation

### Compile the Server

```bash
gcc -Wall -Wextra -pthread -o server_3744 server_3744.c
```

### Compile the Client

```bash
gcc -Wall -Wextra -pthread -o client_3744 client_3744.c
```

## Running the Application

### 1. Start the Server

```bash
./server_3744
```

The server listens on TCP port `9744`.

### 2. Start a Client

From another terminal:

```bash
./client_3744
```

Multiple clients can be started from separate terminals.

### 3. Register a User

```text
REGISTER alice
```

Example:

```text
REGISTER bob
```

### 4. Send Messages

Broadcast:

```text
BCAST Hello everyone
```

Private message:

```text
PMSG bob Hello Bob
```

## File Transfer

To send a file:

```text
SENDFILE bob test.txt
```

The sender transmits a file header followed by the exact file data. The server forwards the file to the intended receiver.

The implementation separates text control messages from raw file bytes using line-based and exact-byte socket handling.

## Concurrency

The server creates a separate POSIX thread for each connected client.

Shared client information is protected using a mutex to reduce race conditions when clients register, disconnect, or access shared connection information.

## Testing

The application was tested using multiple simultaneous clients.

Testing covered:

* Client connection
* User registration
* Multiple clients
* User listing
* Broadcast messaging
* Private messaging
* Room creation/joining
* Room messaging
* Leaving rooms
* File transfer
* Normal disconnection
* Unexpected disconnection
* Invalid commands
* Invalid users and rooms
* Missing files

## Build Requirements

A Linux environment with the following tools is required:

* GCC
* POSIX socket support
* POSIX threads
* Standard C libraries

## Authors

IE3010-NetMessenger Project

NID: 3744

## Academic Project

This project was developed as part of the IE3010 networking/system programming assignment.

