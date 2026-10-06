#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/stat.h>

#define PORT 9744

int client_fd;

/* Receive one line from server */
int recv_line(int socket_fd, char *buffer, size_t buffer_size)
{
    size_t index = 0;

    while (index < buffer_size - 1)
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
            buffer[index++] = character;
        }
    }

    buffer[index] = '\0';

    return 1;
}

/* Receive an exact number of bytes */
int recv_exact(
    int socket_fd,
    char *buffer,
    size_t bytes_to_receive
)
{
    size_t total_received = 0;

    while (total_received < bytes_to_receive)
    {
        ssize_t received = recv(
            socket_fd,
            buffer + total_received,
            bytes_to_receive - total_received,
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

/* Send all requested bytes */
int send_exact(
    int socket_fd,
    const char *buffer,
    size_t bytes_to_send
)
{
    size_t total_sent = 0;

    while (total_sent < bytes_to_send)
    {
        ssize_t sent = send(
            socket_fd,
            buffer + total_sent,
            bytes_to_send - total_sent,
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

/* Receive messages from server */
void *receive_messages(void *arg)
{
    (void)arg;

    char response[1024];

    while (1)
    {
        int result = recv_line(
            client_fd,
            response,
            sizeof(response)
        );

        if (result == 0)
        {
            printf("\nDisconnected from server.\n");
            break;
        }

        if (result == -1)
        {
            perror("recv");
            break;
        }

        if (strlen(response) == 0)
        {
            continue;
        }

        /*
         * File header format:
         *
         * FILESTART <sender> <filename> <size>
         */
        if (strncmp(response, "FILESTART ", 10) == 0)
        {
            char sender[50];
            char filename[512];
            long file_size;

            if (sscanf(
                    response + 10,
                    "%49s %511s %ld",
                    sender,
                    filename,
                    &file_size
                ) == 3)
            {
                printf(
                    "\nReceiving file from %s: %s (%ld bytes)\n",
                    sender,
                    filename,
                    file_size
                );

                if (file_size < 0)
                {
                    printf("Invalid file size.\n");
                    continue;
                }

                FILE *file = fopen(
                    filename,
                    "wb"
                );

                if (file == NULL)
                {
                    perror("fopen");
                    continue;
                }

                char file_buffer[4096];

                long total_received = 0;

                while (total_received < file_size)
                {
                    long remaining =
                        file_size - total_received;

                    size_t chunk_size =
                        sizeof(file_buffer);

                    if (remaining <
                        (long)chunk_size)
                    {
                        chunk_size =
                            (size_t)remaining;
                    }

                    int result = recv_exact(
                        client_fd,
                        file_buffer,
                        chunk_size
                    );

                    if (result == 0)
                    {
                        printf(
                            "Server disconnected during file transfer.\n"
                        );

                        break;
                    }

                    if (result == -1)
                    {
                        perror("recv file");
                        break;
                    }

                    size_t written = fwrite(
                        file_buffer,
                        1,
                        chunk_size,
                        file
                    );

                    if (written != chunk_size)
                    {
                        perror("fwrite");
                        break;
                    }

                    total_received +=
                        (long)chunk_size;

                    printf(
                        "Receiving: %ld/%ld bytes\n",
                        total_received,
                        file_size
                    );
                }

                fclose(file);

                if (total_received == file_size)
                {
                    printf(
                        "File received successfully: %s (%ld bytes)\n",
                        filename,
                        total_received
                    );
                }
                else
                {
                    printf(
                        "File transfer incomplete: %ld/%ld bytes\n",
                        total_received,
                        file_size
                    );
                }

                printf("> ");
                fflush(stdout);

                continue;
            }
        }

        printf(
            "\n%s\n",
            response
        );

        printf("> ");
        fflush(stdout);
    }

    return NULL;
}

int main(void)
{
    struct sockaddr_in server_address;

    /* Create socket */
    client_fd = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (client_fd == -1)
    {
        perror("socket");
        return 1;
    }

    /* Configure server address */
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr =
        inet_addr("127.0.0.1");

    /* Connect to server */
    if (connect(
            client_fd,
            (struct sockaddr *)&server_address,
            sizeof(server_address)
        ) == -1)
    {
        perror("connect");
        close(client_fd);
        return 1;
    }

    printf("Connected to server.\n");

    /* Start receiver thread */
    pthread_t receiver_thread;

    if (pthread_create(
            &receiver_thread,
            NULL,
            receive_messages,
            NULL
        ) != 0)
    {
        perror("pthread_create");
        close(client_fd);
        return 1;
    }

    char message[1024];

    /* Main command loop */
    while (1)
    {
        printf("> ");
        fflush(stdout);

        if (fgets(
                message,
                sizeof(message),
                stdin
            ) == NULL)
        {
            break;
        }

        /* Remove newline */
        message[strcspn(message, "\n")] = '\0';

        if (strlen(message) == 0)
        {
            continue;
        }

        /*
         * SENDFILE
         *
         * Format:
         * SENDFILE <username> <filename>
         */
        if (strncmp(message, "SENDFILE ", 9) == 0)
        {
            char target_username[32];
            char filename[512];

            if (sscanf(
                    message + 9,
                    "%31s %511s",
                    target_username,
                    filename
                ) != 2)
            {
                printf(
                    "Usage: SENDFILE <username> <filename>\n"
                );

                continue;
            }

            /* Open file */
            FILE *file = fopen(
                filename,
                "rb"
            );

            if (file == NULL)
            {
                perror("fopen");
                continue;
            }

            /* Get file information */
            struct stat file_info;

            if (stat(
                    filename,
                    &file_info
                ) == -1)
            {
                perror("stat");
                fclose(file);
                continue;
            }

            long file_size =
                (long)file_info.st_size;

            /* Create file header */
            char header[1024];

            snprintf(
                header,
                sizeof(header),
                "FILESTART %s %s %ld\n",
                target_username,
                filename,
                file_size
            );

            /* Send file header */
            if (send_exact(
                    client_fd,
                    header,
                    strlen(header)
                ) == -1)
            {
                perror("send");
                fclose(file);
                break;
            }

            printf(
                "Sending file: %s (%ld bytes) to %s\n",
                filename,
                file_size,
                target_username
            );

            /* Send file data */
            char file_buffer[4096];

            long total_sent = 0;

            while (total_sent < file_size)
            {
                size_t bytes_to_read =
                    sizeof(file_buffer);

                if (file_size - total_sent <
                    (long)sizeof(file_buffer))
                {
                    bytes_to_read =
                        (size_t)(
                            file_size - total_sent
                        );
                }

                size_t bytes_read = fread(
                    file_buffer,
                    1,
                    bytes_to_read,
                    file
                );

                if (bytes_read == 0)
                {
                    if (ferror(file))
                    {
                        perror("fread");
                    }

                    break;
                }

                if (send_exact(
                        client_fd,
                        file_buffer,
                        bytes_read
                    ) == -1)
                {
                    perror("send");
                    fclose(file);
                    close(client_fd);
                    return 1;
                }

                total_sent +=
                    (long)bytes_read;
            }

            fclose(file);

            if (total_sent == file_size)
            {
                printf(
                    "File sent successfully: %ld bytes\n",
                    total_sent
                );
            }
            else
            {
                printf(
                    "File transfer incomplete: %ld/%ld bytes\n",
                    total_sent,
                    file_size
                );
            }

            continue;
        }

        /*
         * Normal command.
         *
         * Server expects commands in this format:
         *
         * REGISTER username\n
         * LIST\n
         * BCAST message\n
         * PMSG username message\n
         * JOIN room\n
         * LEAVE room\n
         * ROOMS\n
         * RMSG room message\n
         * QUIT\n
         */

        char command[1025];

        snprintf(
            command,
            sizeof(command),
            "%s\n",
            message
        );

        if (send_exact(
                client_fd,
                command,
                strlen(command)
            ) == -1)
        {
            perror("send");
            break;
        }

        /* Quit */
        if (strcmp(message, "QUIT") == 0)
        {
            printf("Disconnecting...\n");
            break;
        }
    }

    close(client_fd);

    return 0;
}
