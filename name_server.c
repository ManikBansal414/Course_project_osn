#include "common.h"
#include <signal.h>
#include <stdarg.h>

#define NM_PORT 8080
#define METADATA_FILE "nm_metadata.dat"

// Global data structures
typedef struct FileNode {
    FileMetadata metadata;
    UserAccess* access_list;
    int access_count;
    struct FileNode* next;
} FileNode;

typedef struct {
    char key[MAX_FILENAME];
    FileNode* file;
} HashEntry;

typedef struct {
    char username[MAX_USERNAME];
    char ip[INET_ADDRSTRLEN];
    int sock;
    time_t connected_time;
} ClientInfo;

// LRU Cache Node
typedef struct CacheNode {
    char filename[MAX_FILENAME];
    FileNode* file;
    struct CacheNode* prev;
    struct CacheNode* next;
} CacheNode;

typedef struct {
    CacheNode* head;
    CacheNode* tail;
    int size;
    HashEntry* cache_map[LRU_CACHE_SIZE];
} LRUCache;

// Global variables
FileNode* file_list = NULL;
HashEntry* file_hash[MAX_FILES];
StorageServerInfo storage_servers[MAX_STORAGE_SERVERS];
ClientInfo clients[MAX_CLIENTS];
int num_storage_servers = 0;
int num_clients = 0;
pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;
LRUCache cache;
FILE* log_file = NULL;

// Function prototypes
void init_cache();
FileNode* cache_get(const char* filename);
void cache_put(const char* filename, FileNode* file);
unsigned int hash_function(const char* str);
FileNode* find_file(const char* filename);
void add_file(FileMetadata* metadata);
int get_user_access(FileNode* file, const char* username);
void* handle_client(void* arg);
void* handle_storage_server(void* arg);
void save_metadata();
void load_metadata();
void log_to_file(const char* format, ...);

// Initialize LRU cache
void init_cache() {
    cache.head = NULL;
    cache.tail = NULL;
    cache.size = 0;
    memset(cache.cache_map, 0, sizeof(cache.cache_map));
}

// Hash function for efficient file lookup
unsigned int hash_function(const char* str) {
    unsigned int hash = 5381;
    int c;
    while ((c = *str++)) {
        hash = ((hash << 5) + hash) + c;
    }
    return hash % MAX_FILES;
}

// Find file using hash table
FileNode* find_file(const char* filename) {
    // Check cache first
    FileNode* cached = cache_get(filename);
    if (cached) {
        log_message("NM", "Cache hit for file: %s", filename);
        return cached;
    }
    
    unsigned int index = hash_function(filename);
    HashEntry* entry = file_hash[index];
    
    while (entry) {
        if (strcmp(entry->key, filename) == 0) {
            cache_put(filename, entry->file);
            return entry->file;
        }
        entry = (HashEntry*)entry->file->next;
    }
    
    return NULL;
}

// LRU Cache operations
FileNode* cache_get(const char* filename) {
    unsigned int index = hash_function(filename) % LRU_CACHE_SIZE;
    HashEntry* entry = cache.cache_map[index];
    
    if (entry && strcmp(entry->key, filename) == 0) {
        return entry->file;
    }
    return NULL;
}

void cache_put(const char* filename, FileNode* file) {
    unsigned int index = hash_function(filename) % LRU_CACHE_SIZE;
    
    HashEntry* entry = (HashEntry*)malloc(sizeof(HashEntry));
    strcpy(entry->key, filename);
    entry->file = file;
    cache.cache_map[index] = entry;
}

// Add file to hash table
void add_file(FileMetadata* metadata) {
    unsigned int index = hash_function(metadata->filename);
    
    FileNode* node = (FileNode*)malloc(sizeof(FileNode));
    memcpy(&node->metadata, metadata, sizeof(FileMetadata));
    node->access_list = NULL;
    node->access_count = 0;
    node->next = NULL;
    
    // Add owner with full access
    node->access_list = (UserAccess*)malloc(sizeof(UserAccess));
    strcpy(node->access_list[0].username, metadata->owner);
    node->access_list[0].access_rights = ACCESS_READ | ACCESS_WRITE;
    node->access_count = 1;
    
    if (file_hash[index] == NULL) {
        HashEntry* entry = (HashEntry*)malloc(sizeof(HashEntry));
        strcpy(entry->key, metadata->filename);
        entry->file = node;
        file_hash[index] = entry;
    } else {
        FileNode* current = file_hash[index]->file;
        while (current->next) {
            current = current->next;
        }
        current->next = node;
    }
    
    // Add to linked list
    node->next = file_list;
    file_list = node;
}

// Get user access rights
int get_user_access(FileNode* file, const char* username) {
    for (int i = 0; i < file->access_count; i++) {
        if (strcmp(file->access_list[i].username, username) == 0) {
            return file->access_list[i].access_rights;
        }
    }
    return ACCESS_NONE;
}

// Update file statistics from Storage Server
void update_file_stats(FileNode* file) {
    if (!file || file->metadata.ss_index >= num_storage_servers) return;
    
    int ss_index = file->metadata.ss_index;
    int ss_sock = connect_to_server(storage_servers[ss_index].ip, 
        storage_servers[ss_index].nm_port);
    
    if (ss_sock < 0) return;
    
    Message msg;
    memset(&msg, 0, sizeof(msg));
    msg.type = MSG_SS_STAT;
    strcpy(msg.filename, file->metadata.filename);
    
    send_message(ss_sock, &msg);
    
    Message response;
    if (receive_message(ss_sock, &response) == 0 && response.error_code == ERR_SUCCESS) {
        // Response data contains: word_count char_count
        sscanf(response.data, "%d %d", &file->metadata.word_count, &file->metadata.char_count);
    }
    
    close(ss_sock);
}

// Log to file with timestamp
void log_to_file(const char* format, ...) {
    if (!log_file) return;
    
    time_t now;
    time(&now);
    char time_str[64];
    strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    fprintf(log_file, "[%s] ", time_str);
    
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
}

// Handle VIEW command
void handle_view(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    int show_all = (msg->flags & 1);
    int show_details = (msg->flags & 2);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    response.error_code = ERR_SUCCESS;
    
    char buffer[MAX_BUFFER_SIZE] = {0};
    int offset = 0;
    
    if (show_details) {
        offset += sprintf(buffer + offset, 
            "---------------------------------------------------------\n"
            "|  Filename  | Words | Chars | Last Access Time | Owner |\n"
            "|------------|-------|-------|------------------|-------|\n");
    }
    
    FileNode* current = file_list;
    while (current && offset < MAX_BUFFER_SIZE - 1024) {
        int access = get_user_access(current, msg->username);
        
        if (show_all || access != ACCESS_NONE) {
            // Update stats if showing details
            if (show_details) {
                update_file_stats(current);
            }
            
            if (show_details) {
                char time_str[32];
                format_time(current->metadata.last_accessed, time_str, sizeof(time_str));
                offset += sprintf(buffer + offset, "| %-10s | %5d | %5d | %16s | %5s |\n",
                    current->metadata.filename,
                    current->metadata.word_count,
                    current->metadata.char_count,
                    time_str,
                    current->metadata.owner);
            } else {
                offset += sprintf(buffer + offset, "--> %s\n", current->metadata.filename);
            }
        }
        current = current->next;
    }
    
    if (show_details) {
        offset += sprintf(buffer + offset, 
            "---------------------------------------------------------\n");
    }
    
    strcpy(response.data, buffer);
    response.data_len = strlen(buffer);
    
    pthread_mutex_unlock(&data_mutex);
    
    send_message(client_sock, &response);
    log_to_file("VIEW request from %s, flags=%d", msg->username, msg->flags);
}

// Handle INFO command
void handle_info(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* file = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (!file) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: File not found");
    } else {
        int access = get_user_access(file, msg->username);
        if (access == ACCESS_NONE) {
            response.error_code = ERR_UNAUTHORIZED;
            strcpy(response.data, "ERROR: Unauthorized access");
        } else {
            // Update file stats first
            update_file_stats(file);
            
            response.error_code = ERR_SUCCESS;
            char buffer[MAX_BUFFER_SIZE];
            char created_str[32], modified_str[32], accessed_str[32];
            
            format_time(file->metadata.created, created_str, sizeof(created_str));
            format_time(file->metadata.last_modified, modified_str, sizeof(modified_str));
            format_time(file->metadata.last_accessed, accessed_str, sizeof(accessed_str));
            
            int offset = sprintf(buffer, 
                "--> File: %s\n"
                "--> Owner: %s\n"
                "--> Created: %s\n"
                "--> Last Modified: %s\n"
                "--> Size: %d bytes\n",
                file->metadata.filename,
                file->metadata.owner,
                created_str,
                modified_str,
                file->metadata.char_count);
            
            offset += sprintf(buffer + offset, "--> Access: ");
            for (int i = 0; i < file->access_count; i++) {
                offset += sprintf(buffer + offset, "%s (%s)%s",
                    file->access_list[i].username,
                    (file->access_list[i].access_rights & ACCESS_WRITE) ? "RW" : "R",
                    (i < file->access_count - 1) ? ", " : "");
            }
            offset += sprintf(buffer + offset, "\n--> Last Accessed: %s by %s\n",
                accessed_str, file->metadata.owner);
            
            strcpy(response.data, buffer);
            response.data_len = strlen(buffer);
        }
    }
    
    pthread_mutex_unlock(&data_mutex);
    
    send_message(client_sock, &response);
    log_to_file("INFO request from %s for file %s", msg->username, msg->filename);
}

// Handle LIST USERS command
void handle_list_users(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    response.error_code = ERR_SUCCESS;
    
    char buffer[MAX_BUFFER_SIZE] = {0};
    int offset = 0;
    
    // Collect unique usernames
    char usernames[MAX_CLIENTS][MAX_USERNAME];
    int user_count = 0;
    
    // First, add all currently connected clients
    for (int i = 0; i < num_clients; i++) {
        int found = 0;
        for (int j = 0; j < user_count; j++) {
            if (strcmp(usernames[j], clients[i].username) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && user_count < MAX_CLIENTS) {
            strcpy(usernames[user_count++], clients[i].username);
        }
    }
    
    // Then add users from file metadata
    FileNode* current = file_list;
    while (current) {
        // Add owner
        int found = 0;
        for (int i = 0; i < user_count; i++) {
            if (strcmp(usernames[i], current->metadata.owner) == 0) {
                found = 1;
                break;
            }
        }
        if (!found && user_count < MAX_CLIENTS) {
            strcpy(usernames[user_count++], current->metadata.owner);
        }
        
        // Add users with access
        for (int i = 0; i < current->access_count; i++) {
            found = 0;
            for (int j = 0; j < user_count; j++) {
                if (strcmp(usernames[j], current->access_list[i].username) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found && user_count < MAX_CLIENTS) {
                strcpy(usernames[user_count++], current->access_list[i].username);
            }
        }
        current = current->next;
    }
    
    for (int i = 0; i < user_count && offset < MAX_BUFFER_SIZE - 128; i++) {
        offset += sprintf(buffer + offset, "--> %s\n", usernames[i]);
    }
    
    strcpy(response.data, buffer);
    response.data_len = strlen(buffer);
    
    pthread_mutex_unlock(&data_mutex);
    
    send_message(client_sock, &response);
    log_to_file("LIST USERS request from %s", msg->username);
}

// Handle access control commands
void handle_access_control(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* file = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (!file) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: File not found");
    } else if (strcmp(file->metadata.owner, msg->username) != 0) {
        response.error_code = ERR_UNAUTHORIZED;
        strcpy(response.data, "ERROR: Only owner can modify access");
    } else {
        response.error_code = ERR_SUCCESS;
        
        if (msg->type == MSG_ADD_ACCESS) {
            // Find if user already has access
            int found = -1;
            for (int i = 0; i < file->access_count; i++) {
                if (strcmp(file->access_list[i].username, msg->target_user) == 0) {
                    found = i;
                    break;
                }
            }
            
            int new_rights = (msg->flags == 1) ? ACCESS_READ : (ACCESS_READ | ACCESS_WRITE);
            
            if (found >= 0) {
                file->access_list[found].access_rights = new_rights;
            } else {
                file->access_list = (UserAccess*)realloc(file->access_list, 
                    (file->access_count + 1) * sizeof(UserAccess));
                strcpy(file->access_list[file->access_count].username, msg->target_user);
                file->access_list[file->access_count].access_rights = new_rights;
                file->access_count++;
            }
            strcpy(response.data, "Access granted successfully!");
        } else if (msg->type == MSG_REM_ACCESS) {
            int found = -1;
            for (int i = 0; i < file->access_count; i++) {
                if (strcmp(file->access_list[i].username, msg->target_user) == 0) {
                    found = i;
                    break;
                }
            }
            
            if (found >= 0 && strcmp(msg->target_user, file->metadata.owner) != 0) {
                for (int i = found; i < file->access_count - 1; i++) {
                    file->access_list[i] = file->access_list[i + 1];
                }
                file->access_count--;
                strcpy(response.data, "Access removed successfully!");
            } else {
                response.error_code = ERR_INVALID_COMMAND;
                strcpy(response.data, "ERROR: Cannot remove owner access or user not found");
            }
        }
        
        save_metadata();
    }
    
    pthread_mutex_unlock(&data_mutex);
    
    send_message(client_sock, &response);
    log_to_file("ACCESS CONTROL from %s for file %s, target %s", 
        msg->username, msg->filename, msg->target_user);
}

// Handle CREATE command
void handle_create(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* existing = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (existing) {
        response.error_code = ERR_FILE_EXISTS;
        strcpy(response.data, "ERROR: File already exists");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    // Find available storage server
    int ss_index = -1;
    for (int i = 0; i < num_storage_servers; i++) {
        if (storage_servers[i].is_active) {
            ss_index = i;
            break;
        }
    }
    
    if (ss_index < 0) {
        response.error_code = ERR_NO_STORAGE_SERVER;
        strcpy(response.data, "ERROR: No storage server available");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    pthread_mutex_unlock(&data_mutex);
    
    // Forward to storage server
    int ss_sock = connect_to_server(storage_servers[ss_index].ip, 
        storage_servers[ss_index].nm_port);
    
    if (ss_sock < 0) {
        response.error_code = ERR_CONNECTION_FAILED;
        strcpy(response.data, "ERROR: Cannot connect to storage server");
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    memset(&ss_msg, 0, sizeof(ss_msg));
    ss_msg.type = MSG_SS_CREATE;
    strcpy(ss_msg.filename, msg->filename);
    strcpy(ss_msg.username, msg->username);
    
    send_message(ss_sock, &ss_msg);
    
    Message ss_response;
    if (receive_message(ss_sock, &ss_response) == 0) {
        if (ss_response.error_code == ERR_SUCCESS) {
            // Add to metadata
            pthread_mutex_lock(&data_mutex);
            FileMetadata metadata;
            strcpy(metadata.filename, msg->filename);
            strcpy(metadata.owner, msg->username);
            time(&metadata.created);
            metadata.last_modified = metadata.created;
            metadata.last_accessed = metadata.created;
            metadata.word_count = 0;
            metadata.char_count = 0;
            metadata.ss_index = ss_index;
            
            add_file(&metadata);
            save_metadata();
            pthread_mutex_unlock(&data_mutex);
            
            response.error_code = ERR_SUCCESS;
            strcpy(response.data, "File Created Successfully!");
        } else {
            response.error_code = ss_response.error_code;
            strcpy(response.data, ss_response.data);
        }
    } else {
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Storage server communication failed");
    }
    
    close(ss_sock);
    send_message(client_sock, &response);
    log_to_file("CREATE request from %s for file %s", msg->username, msg->filename);
}

// Handle DELETE command
void handle_delete(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* file = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (!file) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: File not found");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    if (strcmp(file->metadata.owner, msg->username) != 0) {
        response.error_code = ERR_UNAUTHORIZED;
        strcpy(response.data, "ERROR: Only owner can delete file");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    int ss_index = file->metadata.ss_index;
    pthread_mutex_unlock(&data_mutex);
    
    // Forward to storage server
    int ss_sock = connect_to_server(storage_servers[ss_index].ip, 
        storage_servers[ss_index].nm_port);
    
    if (ss_sock < 0) {
        response.error_code = ERR_CONNECTION_FAILED;
        strcpy(response.data, "ERROR: Cannot connect to storage server");
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    memset(&ss_msg, 0, sizeof(ss_msg));
    ss_msg.type = MSG_SS_DELETE;
    strcpy(ss_msg.filename, msg->filename);
    
    send_message(ss_sock, &ss_msg);
    
    Message ss_response;
    if (receive_message(ss_sock, &ss_response) == 0) {
        if (ss_response.error_code == ERR_SUCCESS) {
            // Remove from metadata
            pthread_mutex_lock(&data_mutex);
            
            // Remove from hash table and linked list
            FileNode* prev = NULL;
            FileNode* current = file_list;
            while (current) {
                if (strcmp(current->metadata.filename, msg->filename) == 0) {
                    if (prev) {
                        prev->next = current->next;
                    } else {
                        file_list = current->next;
                    }
                    free(current->access_list);
                    free(current);
                    break;
                }
                prev = current;
                current = current->next;
            }
            
            // Clear from hash table
            unsigned int index = hash_function(msg->filename);
            file_hash[index] = NULL;
            
            save_metadata();
            pthread_mutex_unlock(&data_mutex);
            
            response.error_code = ERR_SUCCESS;
            sprintf(response.data, "File '%s' deleted successfully!", msg->filename);
        } else {
            response.error_code = ss_response.error_code;
            strcpy(response.data, ss_response.data);
        }
    } else {
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Storage server communication failed");
    }
    
    close(ss_sock);
    send_message(client_sock, &response);
    log_to_file("DELETE request from %s for file %s", msg->username, msg->filename);
}

// Handle READ/WRITE/STREAM commands - return SS info to client
void handle_direct_ss_operation(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* file = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (!file) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: File not found");
    } else {
        int access = get_user_access(file, msg->username);
        int required_access = (msg->type == MSG_WRITE_FILE) ? ACCESS_WRITE : ACCESS_READ;
        
        if ((access & required_access) == 0) {
            response.error_code = ERR_UNAUTHORIZED;
            strcpy(response.data, "ERROR: Unauthorized access");
        } else {
            response.error_code = ERR_SUCCESS;
            strcpy(response.ss_ip, storage_servers[file->metadata.ss_index].ip);
            response.ss_port = storage_servers[file->metadata.ss_index].client_port;
            sprintf(response.data, "Connect to SS at %s:%d", response.ss_ip, response.ss_port);
        }
    }
    
    pthread_mutex_unlock(&data_mutex);
    
    send_message(client_sock, &response);
    log_to_file("SS lookup from %s for file %s, operation %d", 
        msg->username, msg->filename, msg->type);
}

// Handle EXEC command
void handle_exec(int client_sock, Message* msg) {
    pthread_mutex_lock(&data_mutex);
    
    FileNode* file = find_file(msg->filename);
    
    Message response;
    memset(&response, 0, sizeof(response));
    response.type = MSG_RESPONSE;
    
    if (!file) {
        response.error_code = ERR_FILE_NOT_FOUND;
        strcpy(response.data, "ERROR: File not found");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    int access = get_user_access(file, msg->username);
    if ((access & ACCESS_READ) == 0) {
        response.error_code = ERR_UNAUTHORIZED;
        strcpy(response.data, "ERROR: Unauthorized access");
        pthread_mutex_unlock(&data_mutex);
        send_message(client_sock, &response);
        return;
    }
    
    int ss_index = file->metadata.ss_index;
    pthread_mutex_unlock(&data_mutex);
    
    // Get file content from SS
    int ss_sock = connect_to_server(storage_servers[ss_index].ip, 
        storage_servers[ss_index].nm_port);
    
    if (ss_sock < 0) {
        response.error_code = ERR_CONNECTION_FAILED;
        strcpy(response.data, "ERROR: Cannot connect to storage server");
        send_message(client_sock, &response);
        return;
    }
    
    Message ss_msg;
    memset(&ss_msg, 0, sizeof(ss_msg));
    ss_msg.type = MSG_SS_READ;
    strcpy(ss_msg.filename, msg->filename);
    
    send_message(ss_sock, &ss_msg);
    
    Message ss_response;
    if (receive_message(ss_sock, &ss_response) == 0 && ss_response.error_code == ERR_SUCCESS) {
        // Execute commands
        char output[MAX_BUFFER_SIZE] = {0};
        FILE* fp = popen(ss_response.data, "r");
        if (fp) {
            size_t n = fread(output, 1, sizeof(output) - 1, fp);
            output[n] = '\0';
            pclose(fp);
            
            response.error_code = ERR_SUCCESS;
            strcpy(response.data, output);
        } else {
            response.error_code = ERR_SERVER_ERROR;
            strcpy(response.data, "ERROR: Command execution failed");
        }
    } else {
        response.error_code = ERR_SERVER_ERROR;
        strcpy(response.data, "ERROR: Cannot read file from storage server");
    }
    
    close(ss_sock);
    send_message(client_sock, &response);
    log_to_file("EXEC request from %s for file %s", msg->username, msg->filename);
}

// Handle client connection
void* handle_client(void* arg) {
    int client_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    while (receive_message(client_sock, &msg) == 0) {
        log_message("NM", "Received message type %d from client %s", msg.type, msg.username);
        
        switch (msg.type) {
            case MSG_REGISTER_CLIENT:
                pthread_mutex_lock(&data_mutex);
                if (num_clients < MAX_CLIENTS) {
                    strcpy(clients[num_clients].username, msg.username);
                    clients[num_clients].sock = client_sock;
                    time(&clients[num_clients].connected_time);
                    num_clients++;
                    
                    Message response;
                    memset(&response, 0, sizeof(response));
                    response.type = MSG_ACK;
                    response.error_code = ERR_SUCCESS;
                    strcpy(response.data, "Client registered successfully");
                    send_message(client_sock, &response);
                    
                    log_message("NM", "Client %s registered", msg.username);
                }
                pthread_mutex_unlock(&data_mutex);
                break;
                
            case MSG_REGISTER_SS:
                pthread_mutex_lock(&data_mutex);
                if (num_storage_servers < MAX_STORAGE_SERVERS) {
                    strcpy(storage_servers[num_storage_servers].ip, msg.ss_ip);
                    storage_servers[num_storage_servers].nm_port = msg.ss_port;
                    storage_servers[num_storage_servers].client_port = msg.flags;
                    storage_servers[num_storage_servers].is_active = 1;
                    time(&storage_servers[num_storage_servers].last_heartbeat);
                    
                    // Parse file list from msg.data and add to metadata
                    char data_copy[MAX_BUFFER_SIZE];
                    strcpy(data_copy, msg.data);
                    char* token = strtok(data_copy, "\n");
                    while (token) {
                        // Check if file already exists
                        if (!find_file(token)) {
                            FileMetadata metadata;
                            strcpy(metadata.filename, token);
                            strcpy(metadata.owner, "system");
                            time(&metadata.created);
                            metadata.last_modified = metadata.created;
                            metadata.last_accessed = metadata.created;
                            metadata.word_count = 0;
                            metadata.char_count = 0;
                            metadata.ss_index = num_storage_servers;
                            
                            add_file(&metadata);
                        }
                        token = strtok(NULL, "\n");
                    }
                    
                    Message response;
                    memset(&response, 0, sizeof(response));
                    response.type = MSG_ACK;
                    response.error_code = ERR_SUCCESS;
                    sprintf(response.data, "Storage Server registered successfully (index: %d)", 
                        num_storage_servers);
                    send_message(client_sock, &response);
                    
                    log_message("NM", "Storage Server %s:%d registered (index %d)", 
                        msg.ss_ip, msg.ss_port, num_storage_servers);
                    log_to_file("Storage Server %s:%d registered", msg.ss_ip, msg.ss_port);
                    
                    num_storage_servers++;
                    save_metadata();
                }
                pthread_mutex_unlock(&data_mutex);
                break;
                
            case MSG_VIEW_FILES:
                handle_view(client_sock, &msg);
                break;
                
            case MSG_INFO_FILE:
                handle_info(client_sock, &msg);
                break;
                
            case MSG_LIST_USERS:
                handle_list_users(client_sock, &msg);
                break;
                
            case MSG_CREATE_FILE:
                handle_create(client_sock, &msg);
                break;
                
            case MSG_DELETE_FILE:
                handle_delete(client_sock, &msg);
                break;
                
            case MSG_READ_FILE:
            case MSG_WRITE_FILE:
            case MSG_STREAM_FILE:
                handle_direct_ss_operation(client_sock, &msg);
                break;
                
            case MSG_ADD_ACCESS:
            case MSG_REM_ACCESS:
                handle_access_control(client_sock, &msg);
                break;
                
            case MSG_EXEC_FILE:
                handle_exec(client_sock, &msg);
                break;
                
            case MSG_UNDO_FILE:
                handle_direct_ss_operation(client_sock, &msg);
                break;
                
            default:
                log_message("NM", "Unknown message type: %d", msg.type);
        }
    }
    
    log_message("NM", "Client disconnected");
    close(client_sock);
    return NULL;
}

// Handle storage server registration
void* handle_storage_server(void* arg) {
    int ss_sock = *(int*)arg;
    free(arg);
    
    Message msg;
    if (receive_message(ss_sock, &msg) == 0 && msg.type == MSG_REGISTER_SS) {
        pthread_mutex_lock(&data_mutex);
        
        if (num_storage_servers < MAX_STORAGE_SERVERS) {
            strcpy(storage_servers[num_storage_servers].ip, msg.ss_ip);
            storage_servers[num_storage_servers].nm_port = msg.ss_port;
            storage_servers[num_storage_servers].client_port = msg.flags;
            storage_servers[num_storage_servers].is_active = 1;
            time(&storage_servers[num_storage_servers].last_heartbeat);
            
            // Parse file list from msg.data and add to metadata
            char* token = strtok(msg.data, "\n");
            while (token) {
                // Check if file already exists
                if (!find_file(token)) {
                    FileMetadata metadata;
                    strcpy(metadata.filename, token);
                    strcpy(metadata.owner, "system");
                    time(&metadata.created);
                    metadata.last_modified = metadata.created;
                    metadata.last_accessed = metadata.created;
                    metadata.word_count = 0;
                    metadata.char_count = 0;
                    metadata.ss_index = num_storage_servers;
                    
                    add_file(&metadata);
                }
                token = strtok(NULL, "\n");
            }
            
            num_storage_servers++;
            
            Message response;
            memset(&response, 0, sizeof(response));
            response.type = MSG_ACK;
            response.error_code = ERR_SUCCESS;
            sprintf(response.data, "Storage Server registered successfully (index: %d)", 
                num_storage_servers - 1);
            send_message(ss_sock, &response);
            
            log_message("NM", "Storage Server %s:%d registered", msg.ss_ip, msg.ss_port);
            save_metadata();
        }
        
        pthread_mutex_unlock(&data_mutex);
    }
    
    close(ss_sock);
    return NULL;
}

// Save metadata to disk
void save_metadata() {
    FILE* fp = fopen(METADATA_FILE, "wb");
    if (!fp) {
        log_message("NM", "Error saving metadata: %s", strerror(errno));
        return;
    }
    
    FileNode* current = file_list;
    while (current) {
        fwrite(&current->metadata, sizeof(FileMetadata), 1, fp);
        fwrite(&current->access_count, sizeof(int), 1, fp);
        fwrite(current->access_list, sizeof(UserAccess), current->access_count, fp);
        current = current->next;
    }
    
    fclose(fp);
    log_message("NM", "Metadata saved");
}

// Load metadata from disk
void load_metadata() {
    FILE* fp = fopen(METADATA_FILE, "rb");
    if (!fp) {
        log_message("NM", "No existing metadata file");
        return;
    }
    
    while (!feof(fp)) {
        FileMetadata metadata;
        if (fread(&metadata, sizeof(FileMetadata), 1, fp) != 1) break;
        
        int access_count;
        if (fread(&access_count, sizeof(int), 1, fp) != 1) break;
        
        FileNode* node = (FileNode*)malloc(sizeof(FileNode));
        memcpy(&node->metadata, &metadata, sizeof(FileMetadata));
        node->access_count = access_count;
        node->access_list = (UserAccess*)malloc(access_count * sizeof(UserAccess));
        
        if (fread(node->access_list, sizeof(UserAccess), access_count, fp) != (size_t)access_count) {
            free(node->access_list);
            free(node);
            break;
        }
        
        node->next = file_list;
        file_list = node;
        
        // Add to hash table
        unsigned int index = hash_function(metadata.filename);
        if (file_hash[index] == NULL) {
            HashEntry* entry = (HashEntry*)malloc(sizeof(HashEntry));
            strcpy(entry->key, metadata.filename);
            entry->file = node;
            file_hash[index] = entry;
        }
    }
    
    fclose(fp);
    log_message("NM", "Metadata loaded");
}

int main() {
    log_message("NM", "Starting Name Server on port %d", NM_PORT);
    
    // Initialize
    init_cache();
    memset(file_hash, 0, sizeof(file_hash));
    memset(storage_servers, 0, sizeof(storage_servers));
    memset(clients, 0, sizeof(clients));
    
    // Open log file
    log_file = fopen("nm_log.txt", "a");
    if (!log_file) {
        log_message("NM", "Warning: Cannot open log file");
    }
    
    // Load existing metadata
    load_metadata();
    
    // Create server socket
    int server_sock = create_socket(NM_PORT);
    if (server_sock < 0) {
        log_message("NM", "Failed to create server socket");
        return 1;
    }
    
    log_message("NM", "Name Server started successfully");
    
    // Accept connections
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        
        int* client_sock = (int*)malloc(sizeof(int));
        *client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        
        if (*client_sock < 0) {
            log_message("NM", "Error accepting connection: %s", strerror(errno));
            free(client_sock);
            continue;
        }
        
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        log_message("NM", "New connection from %s:%d", client_ip, ntohs(client_addr.sin_port));
        
        // Create thread to handle connection
        pthread_t thread;
        pthread_create(&thread, NULL, handle_client, client_sock);
        pthread_detach(thread);
    }
    
    if (log_file) {
        fclose(log_file);
    }
    
    close(server_sock);
    return 0;
}
