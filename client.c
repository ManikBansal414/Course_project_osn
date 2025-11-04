#include "common.h"

#define NM_IP "127.0.0.1"
#define NM_PORT 8080

char username[MAX_USERNAME];
int nm_sock = -1;

// Function prototypes
void connect_to_nm();
void print_menu();
void handle_command(const char* command);
void cmd_view(const char* args);
void cmd_read(const char* filename);
void cmd_create(const char* filename);
void cmd_write(const char* filename, int sentence_num);
void cmd_delete(const char* filename);
void cmd_info(const char* filename);
void cmd_stream(const char* filename);
void cmd_list();
void cmd_add_access(const char* flag, const char* filename, const char* target_user);
void cmd_rem_access(const char* filename, const char* target_user);
void cmd_exec(const char* filename);
void cmd_undo(const char* filename);

// Connect to Name Server
void connect_to_nm() {
    nm_sock = connect_to_server(NM_IP, NM_PORT);
    if (nm_sock < 0) {
        printf("ERROR: Cannot connect to Name Server at %s:%d\n", NM_IP, NM_PORT);
        exit(1);
    }
    
    // Register client
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_REGISTER_CLIENT;
    strcpy(msg.username, username);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0 && response.error_code == ERR_SUCCESS) {
        printf("✓ Connected to Name Server\n");
        printf("✓ Registered as user: %s\n\n", username);
    } else {
        printf("ERROR: Failed to register with Name Server\n");
        close(nm_sock);
        exit(1);
    }
}

// Print menu
void print_menu() {
    printf("\n═══════════════════════════════════════════════════════════\n");
    printf("                  DISTRIBUTED FILE SYSTEM                  \n");
    printf("═══════════════════════════════════════════════════════════\n");
    printf("Commands:\n");
    printf("  VIEW [-a] [-l] [-al]      - List files\n");
    printf("  READ <filename>           - Read file content\n");
    printf("  CREATE <filename>         - Create new file\n");
    printf("  WRITE <filename> <sent#>  - Write to file\n");
    printf("  DELETE <filename>         - Delete file\n");
    printf("  INFO <filename>           - Get file information\n");
    printf("  STREAM <filename>         - Stream file content\n");
    printf("  LIST                      - List all users\n");
    printf("  ADDACCESS -R/-W <file> <user> - Grant access\n");
    printf("  REMACCESS <file> <user>   - Remove access\n");
    printf("  EXEC <filename>           - Execute file as commands\n");
    printf("  UNDO <filename>           - Undo last change\n");
    printf("  HELP                      - Show this menu\n");
    printf("  EXIT                      - Exit client\n");
    printf("═══════════════════════════════════════════════════════════\n\n");
}

// VIEW command
void cmd_view(const char* args) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_VIEW_FILES;
    strcpy(msg.username, username);
    msg.flags = 0;
    
    if (args) {
        if (strstr(args, "a")) msg.flags |= 1;
        if (strstr(args, "l")) msg.flags |= 2;
    }
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        if (response.error_code == ERR_SUCCESS) {
            printf("%s", response.data);
        } else {
            printf("ERROR: %s\n", response.data);
        }
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// READ command
void cmd_read(const char* filename) {
    // Get SS info from NM
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_READ_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
        printf("ERROR: %s\n", response.data);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_server(response.ss_ip, response.ss_port);
    if (ss_sock < 0) {
        printf("ERROR: Cannot connect to Storage Server\n");
        return;
    }
    
    // Send read request to SS
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_READ_FILE;
    strcpy(msg.filename, filename);
    strcpy(msg.username, username);
    
    send_message(ss_sock, &msg);
    
    // Receive content
    if (receive_message(ss_sock, &response) == 0 && response.error_code == ERR_SUCCESS) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: %s\n", response.data);
    }
    
    close(ss_sock);
}

// CREATE command
void cmd_create(const char* filename) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_CREATE_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// WRITE command
void cmd_write(const char* filename, int sentence_num) {
    // Get SS info from NM
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WRITE_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    msg.flags = sentence_num;
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
        printf("ERROR: %s\n", response.data);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_server(response.ss_ip, response.ss_port);
    if (ss_sock < 0) {
        printf("ERROR: Cannot connect to Storage Server\n");
        return;
    }
    
    // Send write request
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_WRITE_FILE;
    strcpy(msg.filename, filename);
    strcpy(msg.username, username);
    msg.flags = sentence_num;
    
    send_message(ss_sock, &msg);
    
    // Receive lock acknowledgment
    if (receive_message(ss_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
        printf("ERROR: %s\n", response.data);
        close(ss_sock);
        return;
    }
    
    printf("Lock acquired. Enter word updates (word_index content), type ETIRW to finish:\n");
    
    // Read word updates from user
    char line[MAX_BUFFER_SIZE];
    while (1) {
        printf("Client: ");
        fflush(stdout);
        
        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }
        
        // Remove newline
        line[strcspn(line, "\n")] = 0;
        
        if (strcmp(line, "ETIRW") == 0) {
            // Send ETIRW
            memset(&msg, 0, sizeof(msg));
            strcpy(msg.data, "ETIRW");
            send_message(ss_sock, &msg);
            break;
        }
        
        // Parse word_index and content
        int word_index;
        char content[MAX_BUFFER_SIZE];
        
        if (sscanf(line, "%d %[^\n]", &word_index, content) == 2) {
            memset(&msg, 0, sizeof(msg));
            msg.word_index = word_index;
            strcpy(msg.data, content);
            
            send_message(ss_sock, &msg);
            
            // Receive ACK
            if (receive_message(ss_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
                printf("ERROR: %s\n", response.data);
            }
        } else {
            printf("ERROR: Invalid format. Use: <word_index> <content>\n");
        }
    }
    
    // Receive final response
    if (receive_message(ss_sock, &response) == 0) {
        printf("%s\n", response.data);
    }
    
    close(ss_sock);
}

// DELETE command
void cmd_delete(const char* filename) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_DELETE_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// INFO command
void cmd_info(const char* filename) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_INFO_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// STREAM command
void cmd_stream(const char* filename) {
    // Get SS info from NM
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_STREAM_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
        printf("ERROR: %s\n", response.data);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_server(response.ss_ip, response.ss_port);
    if (ss_sock < 0) {
        printf("ERROR: Cannot connect to Storage Server\n");
        return;
    }
    
    // Send stream request
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_STREAM_FILE;
    strcpy(msg.filename, filename);
    strcpy(msg.username, username);
    
    send_message(ss_sock, &msg);
    
    // Receive words
    while (1) {
        if (receive_message(ss_sock, &response) != 0) {
            printf("\nERROR: Storage server disconnected\n");
            break;
        }
        
        if (strcmp(response.data, "STOP") == 0) {
            printf("\n");
            break;
        }
        
        printf("%s ", response.data);
        fflush(stdout);
    }
    
    close(ss_sock);
}

// LIST command
void cmd_list() {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_LIST_USERS;
    strcpy(msg.username, username);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// ADD ACCESS command
void cmd_add_access(const char* flag, const char* filename, const char* target_user) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_ADD_ACCESS;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, target_user);
    msg.flags = (strcmp(flag, "-R") == 0) ? 1 : 2;
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// REMOVE ACCESS command
void cmd_rem_access(const char* filename, const char* target_user) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_REM_ACCESS;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    strcpy(msg.target_user, target_user);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// EXEC command
void cmd_exec(const char* filename) {
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_EXEC_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) == 0) {
        if (response.error_code == ERR_SUCCESS) {
            printf("%s", response.data);
        } else {
            printf("ERROR: %s\n", response.data);
        }
    } else {
        printf("ERROR: Communication failed\n");
    }
}

// UNDO command
void cmd_undo(const char* filename) {
    // Get SS info from NM
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_UNDO_FILE;
    strcpy(msg.username, username);
    strcpy(msg.filename, filename);
    
    send_message(nm_sock, &msg);
    
    Message response;
    if (receive_message(nm_sock, &response) != 0 || response.error_code != ERR_SUCCESS) {
        printf("ERROR: %s\n", response.data);
        return;
    }
    
    // Connect to SS
    int ss_sock = connect_to_server(response.ss_ip, response.ss_port);
    if (ss_sock < 0) {
        printf("ERROR: Cannot connect to Storage Server\n");
        return;
    }
    
    // Send undo request
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_UNDO_FILE;
    strcpy(msg.filename, filename);
    strcpy(msg.username, username);
    
    send_message(ss_sock, &msg);
    
    // Receive response
    if (receive_message(ss_sock, &response) == 0) {
        printf("%s\n", response.data);
    } else {
        printf("ERROR: Communication failed\n");
    }
    
    close(ss_sock);
}

// Handle command
void handle_command(const char* command) {
    char cmd[MAX_BUFFER_SIZE];
    strncpy(cmd, command, sizeof(cmd) - 1);
    cmd[sizeof(cmd) - 1] = '\0';
    
    // Remove trailing newline
    cmd[strcspn(cmd, "\n")] = 0;
    
    // Parse command
    char* token = strtok(cmd, " ");
    if (!token) return;
    
    if (strcasecmp(token, "VIEW") == 0) {
        char* args = strtok(NULL, " ");
        cmd_view(args);
    }
    else if (strcasecmp(token, "READ") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_read(filename);
        } else {
            printf("ERROR: Usage: READ <filename>\n");
        }
    }
    else if (strcasecmp(token, "CREATE") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_create(filename);
        } else {
            printf("ERROR: Usage: CREATE <filename>\n");
        }
    }
    else if (strcasecmp(token, "WRITE") == 0) {
        char* filename = strtok(NULL, " ");
        char* sent_num_str = strtok(NULL, " ");
        if (filename && sent_num_str) {
            int sent_num = atoi(sent_num_str);
            cmd_write(filename, sent_num);
        } else {
            printf("ERROR: Usage: WRITE <filename> <sentence_number>\n");
        }
    }
    else if (strcasecmp(token, "DELETE") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_delete(filename);
        } else {
            printf("ERROR: Usage: DELETE <filename>\n");
        }
    }
    else if (strcasecmp(token, "INFO") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_info(filename);
        } else {
            printf("ERROR: Usage: INFO <filename>\n");
        }
    }
    else if (strcasecmp(token, "STREAM") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_stream(filename);
        } else {
            printf("ERROR: Usage: STREAM <filename>\n");
        }
    }
    else if (strcasecmp(token, "LIST") == 0) {
        cmd_list();
    }
    else if (strcasecmp(token, "ADDACCESS") == 0) {
        char* flag = strtok(NULL, " ");
        char* filename = strtok(NULL, " ");
        char* target_user = strtok(NULL, " ");
        if (flag && filename && target_user) {
            cmd_add_access(flag, filename, target_user);
        } else {
            printf("ERROR: Usage: ADDACCESS -R/-W <filename> <username>\n");
        }
    }
    else if (strcasecmp(token, "REMACCESS") == 0) {
        char* filename = strtok(NULL, " ");
        char* target_user = strtok(NULL, " ");
        if (filename && target_user) {
            cmd_rem_access(filename, target_user);
        } else {
            printf("ERROR: Usage: REMACCESS <filename> <username>\n");
        }
    }
    else if (strcasecmp(token, "EXEC") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_exec(filename);
        } else {
            printf("ERROR: Usage: EXEC <filename>\n");
        }
    }
    else if (strcasecmp(token, "UNDO") == 0) {
        char* filename = strtok(NULL, " ");
        if (filename) {
            cmd_undo(filename);
        } else {
            printf("ERROR: Usage: UNDO <filename>\n");
        }
    }
    else if (strcasecmp(token, "HELP") == 0) {
        print_menu();
    }
    else if (strcasecmp(token, "EXIT") == 0) {
        printf("Goodbye!\n");
        close(nm_sock);
        exit(0);
    }
    else {
        printf("ERROR: Unknown command. Type HELP for list of commands.\n");
    }
}

int main() {
    printf("═══════════════════════════════════════════════════════════\n");
    printf("        DISTRIBUTED FILE SYSTEM - CLIENT                   \n");
    printf("═══════════════════════════════════════════════════════════\n\n");
    
    // Get username
    printf("Enter username: ");
    fflush(stdout);
    if (!fgets(username, sizeof(username), stdin)) {
        printf("ERROR: Cannot read username\n");
        return 1;
    }
    username[strcspn(username, "\n")] = 0;
    
    if (strlen(username) == 0) {
        printf("ERROR: Username cannot be empty\n");
        return 1;
    }
    
    // Connect to Name Server
    connect_to_nm();
    
    // Show menu
    print_menu();
    
    // Command loop
    char command[MAX_BUFFER_SIZE];
    while (1) {
        printf("Client> ");
        fflush(stdout);
        
        if (!fgets(command, sizeof(command), stdin)) {
            break;
        }
        
        handle_command(command);
    }
    
    close(nm_sock);
    return 0;
}
