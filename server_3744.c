#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

#define PORT 9744
#define MAX_CLIENTS 10
#define MAX_USERNAME 50
#define MAX_ROOMS 20
#define MAX_ROOM_NAME 32

typedef struct
{
    char name[MAX_ROOM_NAME];
    int members[MAX_CLIENTS];
    int member_count;
} Room;

Room rooms[MAX_ROOMS];
int room_count = 0;

typedef struct
{
    int socket;
    char username[MAX_USERNAME];
    int registered;
} Client;

Client clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex =
    PTHREAD_MUTEX_INITIALIZER;


/* Register user */
int register_user(int socket, const char *username)
{
    pthread_mutex_lock(&clients_mutex);

    /* Check duplicate username */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].registered &&
            strcmp(clients[i].username, username) == 0)
        {
            pthread_mutex_unlock(&clients_mutex);
            return 0;
        }
    }

    /* Find empty slot */
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (!clients[i].registered)
        {
            clients[i].socket = socket;

            strncpy(
                clients[i].username,
                username,
                MAX_USERNAME - 1
            );

            clients[i].username[MAX_USERNAME - 1] = '\0';
            clients[i].registered = 1;

            pthread_mutex_unlock(&clients_mutex);

            return 1;
        }
    }

    pthread_mutex_unlock(&clients_mutex);

    return -1;
}


/* Remove disconnected client */
void remove_client(int socket)
{
    pthread_mutex_lock(&clients_mutex);

    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (clients[i].registered &&
            clients[i].socket == socket)
        {
            clients[i].registered = 0;
            clients[i].socket = 0;
            clients[i].username[0] = '\0';

            break;
        }
    }

    pthread_mutex_unlock(&clients_mutex);
}


/* Receive one line from TCP socket */
int recv_line(
    int socket_fd,
    char *buffer,
    size_t buffer_size
)
{
    size_t position = 0;

    while (position < buffer_size - 1)
    {
        char character;

        ssize_t received = recv(
            socket_fd,
            &character,
            1,
            0
        );

        if (received == 0)
        {
            return 0;
        }

        if (received == -1)
        {
            return -1;
        }

        if (character == '\n')
        {
            break;
        }

        if (character != '\r')
        {
            buffer[position] = character;
            position++;
        }
    }

    buffer[position] = '\0';

    return 1;
}


/* Receive exactly the requested number of bytes */
int recv_exact(
    int socket_fd,
    char *buffer,
    size_t total_bytes
)
{
    size_t total_received = 0;

    while (total_received < total_bytes)
    {
        ssize_t received = recv(
            socket_fd,
            buffer + total_received,
            total_bytes - total_received,
            0
        );

        if (received == 0)
        {
            return 0;
        }

        if (received == -1)
        {
            return -1;
        }

        total_received += (size_t)received;
    }

    return 1;
}


/* Send exactly the requested number of bytes */
int send_exact(
    int socket_fd,
    const char *buffer,
    size_t total_bytes
)
{
    size_t total_sent = 0;

    while (total_sent < total_bytes)
    {
        ssize_t sent = send(
            socket_fd,
            buffer + total_sent,
            total_bytes - total_sent,
            0
        );

        if (sent == -1)
        {
            return -1;
        }

        total_sent += (size_t)sent;
    }

    return 1;
}


/* Send a text message followed by newline */
int send_line(
    int socket_fd,
    const char *message
)
{
    char line[2048];

    int length = snprintf(
        line,
        sizeof(line),
        "%s\n",
        message
    );

    if (length < 0 ||
        (size_t)length >= sizeof(line))
    {
        return -1;
    }

    return send_exact(
        socket_fd,
        line,
        (size_t)length
    );
}


/* Handle client */
void *handle_client(void *arg)
{
    int client_fd = *(int *)arg;

    free(arg);

    printf("Client connected.\n");

    char buffer[1024];

    while (1)
    {
        memset(
            buffer,
            0,
            sizeof(buffer)
        );

        int line_result = recv_line(
            client_fd,
            buffer,
            sizeof(buffer)
        );

        if (line_result == -1)
        {
            perror("recv");
            break;
        }

        if (line_result == 0)
        {
            printf("Client disconnected.\n");
            break;
        }

        if (strlen(buffer) == 0)
        {
            continue;
        }

        printf(
            "Message from client: %s\n",
            buffer
        );


        /* FILESTART */
        if (strncmp(buffer, "FILESTART ", 10) == 0)
        {
            char target_username[MAX_USERNAME];
            char filename[512];
            long file_size;

            if (sscanf(
                    buffer + 10,
                    "%49s %511s %ld",
                    target_username,
                    filename,
                    &file_size
                ) != 3)
            {
                send_line(
                    client_fd,
                    "ERR Usage: FILESTART <username> <filename> <size>"
                );

                continue;
            }

            if (file_size < 0)
            {
                send_line(
                    client_fd,
                    "ERR Invalid file size"
                );

                continue;
            }

            /* Find sender username */
            char sender[MAX_USERNAME];

            strcpy(
                sender,
                "UNKNOWN"
            );

            /* Find target socket */
            int target_fd = -1;

            pthread_mutex_lock(
                &clients_mutex
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered &&
                    clients[i].socket == client_fd)
                {
                    strncpy(
                        sender,
                        clients[i].username,
                        MAX_USERNAME - 1
                    );

                    sender[MAX_USERNAME - 1] = '\0';
                }

                if (clients[i].registered &&
                    strcmp(
                        clients[i].username,
                        target_username
                    ) == 0)
                {
                    target_fd =
                        clients[i].socket;
                }
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            /* Target not found */
            if (target_fd == -1)
            {
                send_line(
                    client_fd,
                    "ERR User not found"
                );

                continue;
            }

            printf(
                "File transfer started: %s -> %s, %s, %ld bytes\n",
                sender,
                target_username,
                filename,
                file_size
            );

            /*
             * Tell receiver that a file is coming.
             */
            char file_header[1024];

            int header_length = snprintf(
                file_header,
                sizeof(file_header),
                "FILESTART %s %s %ld\n",
                sender,
                filename,
                file_size
            );

            if (header_length < 0 ||
                (size_t)header_length >= sizeof(file_header))
            {
                send_line(
                    client_fd,
                    "ERR File header too long"
                );

                continue;
            }

            if (send_exact(
                    target_fd,
                    file_header,
                    (size_t)header_length
                ) == -1)
            {
                perror("send file header");

                send_line(
                    client_fd,
                    "ERR Could not notify receiver"
                );

                continue;
            }

            /*
             * Tell sender that the server is ready
             * to receive the file.
             */
            if (send_line(
                    client_fd,
                    "OK FILESTART"
                ) == -1)
            {
                perror("send");
                break;
            }

            /*
             * Receive and forward the file in chunks.
             */
            char file_buffer[4096];

            long total_transferred = 0;

            while (total_transferred < file_size)
            {
                long remaining =
                    file_size - total_transferred;

                size_t chunk_size =
                    sizeof(file_buffer);

                if (remaining <
                    (long)chunk_size)
                {
                    chunk_size =
                        (size_t)remaining;
                }

                ssize_t bytes_received = recv(
                    client_fd,
                    file_buffer,
                    chunk_size,
                    0
                );

                if (bytes_received == 0)
                {
                    printf(
                        "Sender disconnected during file transfer.\n"
                    );

                    break;
                }

                if (bytes_received == -1)
                {
                    perror("recv file");
                    break;
                }

                if (send_exact(
                        target_fd,
                        file_buffer,
                        (size_t)bytes_received
                    ) == -1)
                {
                    perror("send file");
                    break;
                }

                total_transferred +=
                    (long)bytes_received;

                printf(
                    "File transfer progress: %ld/%ld bytes\n",
                    total_transferred,
                    file_size
                );
            }

            if (total_transferred == file_size)
            {
                printf(
                    "File transfer completed successfully: %ld bytes\n",
                    total_transferred
                );

                send_line(
                    client_fd,
                    "OK FILESENT"
                );
            }
            else
            {
                printf(
                    "File transfer incomplete: %ld/%ld bytes\n",
                    total_transferred,
                    file_size
                );

                send_line(
                    client_fd,
                    "ERR File transfer incomplete"
                );
            }

            continue;
        }


        /* REGISTER */
        if (strncmp(buffer, "REGISTER ", 9) == 0)
        {
            char username[MAX_USERNAME];

            strncpy(
                username,
                buffer + 9,
                MAX_USERNAME - 1
            );

            username[MAX_USERNAME - 1] = '\0';

            int result =
                register_user(
                    client_fd,
                    username
                );

            char response[1024];

            if (result == 1)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "OK REGISTERED %s",
                    username
                );
            }
            else if (result == 0)
            {
                snprintf(
                    response,
                    sizeof(response),
                    "ERR USERNAME_TAKEN"
                );
            }
            else
            {
                snprintf(
                    response,
                    sizeof(response),
                    "ERR SERVER_FULL"
                );
            }

            send_line(
                client_fd,
                response
            );

            printf(
                "Response sent: %s\n",
                response
            );
        }


        /* LIST */
        else if (strcmp(buffer, "LIST") == 0)
        {
            char response[1024];

            strcpy(
                response,
                "OK USERS "
            );

            pthread_mutex_lock(
                &clients_mutex
            );

            int count = 0;

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered)
                {
                    count++;
                }
            }

            char count_text[20];

            snprintf(
                count_text,
                sizeof(count_text),
                "%d",
                count
            );

            strcat(
                response,
                count_text
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered)
                {
                    char user_line[100];

                    snprintf(
                        user_line,
                        sizeof(user_line),
                        "\n%s ONLINE",
                        clients[i].username
                    );

                    strncat(
                        response,
                        user_line,
                        sizeof(response)
                        - strlen(response)
                        - 1
                    );
                }
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            send_line(
                client_fd,
                response
            );

            printf(
                "LIST response sent:\n%s\n",
                response
            );
        }


        /* BCAST */
        else if (strncmp(buffer, "BCAST ", 6) == 0)
        {
            char message[1024];

            strcpy(
                message,
                buffer + 6
            );

            pthread_mutex_lock(
                &clients_mutex
            );

            char sender[MAX_USERNAME];

            strcpy(
                sender,
                "UNKNOWN"
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered &&
                    clients[i].socket == client_fd)
                {
                    strcpy(
                        sender,
                        clients[i].username
                    );

                    break;
                }
            }

            char broadcast[1024];

            snprintf(
                broadcast,
                sizeof(broadcast),
                "BCAST from %.100s: %.900s",
                sender,
                message
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered)
                {
                    send_line(
                        clients[i].socket,
                        broadcast
                    );
                }
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            printf(
                "Broadcast from %s: %s\n",
                sender,
                message
            );
        }


        /* PMSG */
        else if (strncmp(buffer, "PMSG ", 5) == 0)
        {
            char target_username[MAX_USERNAME];
            char message[1024];

            if (sscanf(
                    buffer + 5,
                    "%49s %[^\n]",
                    target_username,
                    message
                ) < 2)
            {
                send_line(
                    client_fd,
                    "ERR Usage: PMSG <username> <message>"
                );

                continue;
            }

            pthread_mutex_lock(
                &clients_mutex
            );

            char sender[MAX_USERNAME];

            strcpy(
                sender,
                "UNKNOWN"
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered &&
                    clients[i].socket == client_fd)
                {
                    strcpy(
                        sender,
                        clients[i].username
                    );

                    break;
                }
            }

            int target_found = 0;

            char private_message[1024];

            snprintf(
                private_message,
                sizeof(private_message),
                "PMSG from %.100s: %.900s",
                sender,
                message
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered &&
                    strcmp(
                        clients[i].username,
                        target_username
                    ) == 0)
                {
                    send_line(
                        clients[i].socket,
                        private_message
                    );

                    target_found = 1;

                    break;
                }
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            if (!target_found)
            {
                send_line(
                    client_fd,
                    "ERR User not found"
                );
            }

            printf(
                "Private message from %s to %s: %s\n",
                sender,
                target_username,
                message
            );
        }


        /* JOIN */
        else if (strncmp(buffer, "JOIN ", 5) == 0)
        {
            char room_name[MAX_ROOM_NAME];

            if (sscanf(
                    buffer + 5,
                    "%31s",
                    room_name
                ) != 1)
            {
                send_line(
                    client_fd,
                    "ERR Usage: JOIN <room>"
                );

                continue;
            }

            pthread_mutex_lock(
                &clients_mutex
            );

            int room_index = -1;

            for (int i = 0;
                 i < room_count;
                 i++)
            {
                if (strcmp(
                        rooms[i].name,
                        room_name
                    ) == 0)
                {
                    room_index = i;
                    break;
                }
            }

            /* Create room if it does not exist */
            if (room_index == -1)
            {
                if (room_count >= MAX_ROOMS)
                {
                    pthread_mutex_unlock(
                        &clients_mutex
                    );

                    send_line(
                        client_fd,
                        "ERR Maximum number of rooms reached"
                    );

                    continue;
                }

                room_index =
                    room_count++;

                strcpy(
                    rooms[room_index].name,
                    room_name
                );

                rooms[room_index].member_count =
                    0;
            }

            /* Check if user is already in room */
            int already_member = 0;

            for (int i = 0;
                 i < rooms[room_index].member_count;
                 i++)
            {
                if (rooms[room_index].members[i] ==
                    client_fd)
                {
                    already_member = 1;
                    break;
                }
            }

            if (already_member)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR Already in room"
                );

                continue;
            }

            /* Add client to room */
            if (rooms[room_index].member_count >=
                MAX_CLIENTS)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR Room is full"
                );

                continue;
            }

            rooms[room_index].members[
                rooms[room_index].member_count
            ] = client_fd;

            rooms[room_index].member_count++;

            pthread_mutex_unlock(
                &clients_mutex
            );

            char response[128];

            snprintf(
                response,
                sizeof(response),
                "OK JOINED %s",
                room_name
            );

            send_line(
                client_fd,
                response
            );

            printf(
                "Client %d joined room %s\n",
                client_fd,
                room_name
            );
        }


        /* LEAVE */
        else if (strncmp(buffer, "LEAVE ", 6) == 0)
        {
            char room_name[MAX_ROOM_NAME];

            if (sscanf(
                    buffer + 6,
                    "%31s",
                    room_name
                ) != 1)
            {
                send_line(
                    client_fd,
                    "ERR Usage: LEAVE <room>"
                );

                continue;
            }

            pthread_mutex_lock(
                &clients_mutex
            );

            int room_index = -1;

            for (int i = 0;
                 i < room_count;
                 i++)
            {
                if (strcmp(
                        rooms[i].name,
                        room_name
                    ) == 0)
                {
                    room_index = i;
                    break;
                }
            }

            if (room_index == -1)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR Room not found"
                );

                continue;
            }

            int member_index = -1;

            for (int i = 0;
                 i < rooms[room_index].member_count;
                 i++)
            {
                if (rooms[room_index].members[i] ==
                    client_fd)
                {
                    member_index = i;
                    break;
                }
            }

            if (member_index == -1)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR You are not in this room"
                );

                continue;
            }

            /* Remove client from room */
            for (int i = member_index;
                 i <
                 rooms[room_index].member_count - 1;
                 i++)
            {
                rooms[room_index].members[i] =
                    rooms[room_index].members[i + 1];
            }

            rooms[room_index].member_count--;

            pthread_mutex_unlock(
                &clients_mutex
            );

            char response[128];

            snprintf(
                response,
                sizeof(response),
                "OK LEFT %s",
                room_name
            );

            send_line(
                client_fd,
                response
            );

            printf(
                "Client %d left room %s\n",
                client_fd,
                room_name
            );
        }


        /* ROOMS */
        else if (strcmp(buffer, "ROOMS") == 0)
        {
            pthread_mutex_lock(
                &clients_mutex
            );

            char response[2048];

            strcpy(
                response,
                "ROOMS:\n"
            );

            if (room_count == 0)
            {
                strcat(
                    response,
                    "No rooms available"
                );
            }
            else
            {
                for (int i = 0;
                     i < room_count;
                     i++)
                {
                    char room_info[128];

                    snprintf(
                        room_info,
                        sizeof(room_info),
                        "%s (%d users)\n",
                        rooms[i].name,
                        rooms[i].member_count
                    );

                    if (strlen(response) +
                        strlen(room_info) <
                        sizeof(response))
                    {
                        strcat(
                            response,
                            room_info
                        );
                    }
                }
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            send_line(
                client_fd,
                response
            );

            printf(
                "ROOMS requested by client %d\n",
                client_fd
            );
        }


        /* RMSG */
        else if (strncmp(buffer, "RMSG ", 5) == 0)
        {
            char room_name[MAX_ROOM_NAME];
            char message[1024];

            if (sscanf(
                    buffer + 5,
                    "%31s %[^\n]",
                    room_name,
                    message
                ) < 2)
            {
                send_line(
                    client_fd,
                    "ERR Usage: RMSG <room> <message>"
                );

                continue;
            }

            pthread_mutex_lock(
                &clients_mutex
            );

            int room_index = -1;

            for (int i = 0;
                 i < room_count;
                 i++)
            {
                if (strcmp(
                        rooms[i].name,
                        room_name
                    ) == 0)
                {
                    room_index = i;
                    break;
                }
            }

            if (room_index == -1)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR Room not found"
                );

                continue;
            }

            /* Check whether sender is a member */
            int sender_is_member = 0;

            for (int i = 0;
                 i < rooms[room_index].member_count;
                 i++)
            {
                if (rooms[room_index].members[i] ==
                    client_fd)
                {
                    sender_is_member = 1;
                    break;
                }
            }

            if (!sender_is_member)
            {
                pthread_mutex_unlock(
                    &clients_mutex
                );

                send_line(
                    client_fd,
                    "ERR You are not in this room"
                );

                continue;
            }

            /* Find sender username */
            char sender[MAX_USERNAME];

            strcpy(
                sender,
                "UNKNOWN"
            );

            for (int i = 0;
                 i < MAX_CLIENTS;
                 i++)
            {
                if (clients[i].registered &&
                    clients[i].socket == client_fd)
                {
                    strcpy(
                        sender,
                        clients[i].username
                    );

                    break;
                }
            }

            char room_message[1024];

            snprintf(
                room_message,
                sizeof(room_message),
                "RMSG [%s] %s: %.850s",
                room_name,
                sender,
                message
            );

            /* Send to all room members */
            for (int i = 0;
                 i < rooms[room_index].member_count;
                 i++)
            {
                send_line(
                    rooms[room_index].members[i],
                    room_message
                );
            }

            pthread_mutex_unlock(
                &clients_mutex
            );

            printf(
                "Room message from %s in %s: %s\n",
                sender,
                room_name,
                message
            );
        }


        /* QUIT */
        else if (strcmp(buffer, "QUIT") == 0)
        {
            send_line(
                client_fd,
                "OK BYE"
            );

            printf(
                "Client requested disconnect.\n"
            );

            remove_client(
                client_fd
            );

            break;
        }


        /* Unknown command */
        else
        {
            send_line(
                client_fd,
                "ERR UNKNOWN_COMMAND"
            );

            printf(
                "Response sent: ERR UNKNOWN_COMMAND\n"
            );
        }
    }

    close(client_fd);

    return NULL;
}


/* Main server */
int main(void)
{
    int server_fd;

    struct sockaddr_in server_address;

    /* Create socket */
    server_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (server_fd == -1)
    {
        perror("socket");
        return 1;
    }

    printf(
        "Socket created successfully.\n"
    );

    /* Allow address reuse */
    int option = 1;

    if (setsockopt(
            server_fd,
            SOL_SOCKET,
            SO_REUSEADDR,
            &option,
            sizeof(option)
        ) == -1)
    {
        perror("setsockopt");
        close(server_fd);
        return 1;
    }

    memset(
        &server_address,
        0,
        sizeof(server_address)
    );

    server_address.sin_family =
        AF_INET;

    server_address.sin_addr.s_addr =
        INADDR_ANY;

    server_address.sin_port =
        htons(PORT);

    /* Bind */
    if (bind(
            server_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) == -1)
    {
        perror("bind");
        close(server_fd);
        return 1;
    }

    printf(
        "Server bound to port %d.\n",
        PORT
    );

    /* Listen */
    if (listen(
            server_fd,
            MAX_CLIENTS
        ) == -1)
    {
        perror("listen");
        close(server_fd);
        return 1;
    }

    printf(
        "Server is listening for connections...\n"
    );

    while (1)
    {
        struct sockaddr_in client_address;

        socklen_t client_address_length =
            sizeof(client_address);

        int client_fd = accept(
            server_fd,
            (struct sockaddr *)&client_address,
            &client_address_length
        );

        if (client_fd == -1)
        {
            perror("accept");
            continue;
        }

        /*
         * Allocate socket descriptor so that
         * each thread gets its own copy.
         */
        int *client_socket =
            malloc(sizeof(int));

        if (client_socket == NULL)
        {
            perror("malloc");
            close(client_fd);
            continue;
        }

        *client_socket = client_fd;

        pthread_t thread;

        if (pthread_create(
                &thread,
                NULL,
                handle_client,
                client_socket
            ) != 0)
        {
            perror("pthread_create");

            free(client_socket);
            close(client_fd);

            continue;
        }

        pthread_detach(thread);
    }

    close(server_fd);

    return 0;
}
