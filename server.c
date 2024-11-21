#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <syslog.h>


#define MAX 256

// Color codes for terminal output
#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

// Block structure
typedef struct {
    int height;
    int timestamp;
    unsigned int hash;
    unsigned int prev_hash;
    int difficulty;
    int nonce;
    int relayed_by;
} Block_t;

const int READ_END = 0;
const int WRITE_END = 1;

int fd_Server;

typedef enum {
    NEW_MINER = 1,
    NEW_BLOCK = 2
} TLV_TYPE;

typedef struct TLV {
    int type;
    int length;
    char value[1024];
} TLV;

// Function prototypes
Block_t* Initialize_genesis_block(int diff);
bool verify_difficulty(unsigned int hash, int diff);
bool verify_block(Block_t* curr, Block_t* next);
unsigned int calc_hash(Block_t* block);
void print_block(Block_t* block);

TLV* readTlvFromPipe(int pipeReadEnd);
void writeTlvToPipe(int pipeWriteEnd, TLV* tlv);

void cleanup_pipes();
void signal_handler(int signum);
int read_difficulty_from_file(const char* filepath);

// Log file pointer
FILE *log_file;

// Log function
void log_message(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(log_file, format, args);
    fprintf(log_file, "\n");
    fflush(log_file);
    va_end(args);
}

int main(int argc, char* argv[]) 
{

    // Open log file
    log_file = fopen("/var/log/mtacoin.log", "a");
    if (!log_file) {
        log_message("Error opening log file");
        exit(EXIT_FAILURE);
    }

    int miners_Count = 0;
    
    const char* config_file = "/mnt/mta/mtacoin.conf";  // Path to the configuration file
    int difficulty = read_difficulty_from_file(config_file);

    log_message("reading %s...\n", config_file);

    if (difficulty < 0 || difficulty > 31) {
        log_message(ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET "Difficulty parameter is not in range (0-31)\n");
        exit(1);
    }

    log_message("Difficulty set to %d\n", difficulty);

    // Initialize the genesis block
    Block_t* current_block = Initialize_genesis_block(difficulty);

    // Prepare the next block
    Block_t* next_block = (Block_t*)malloc(sizeof(Block_t));
    next_block->height = current_block->height;
    next_block->hash = current_block->hash;
    next_block->prev_hash = current_block->prev_hash;
    next_block->difficulty = current_block->difficulty;
    next_block->nonce = current_block->nonce;
    next_block->relayed_by = current_block->relayed_by;


    if (mkfifo("/mnt/mta/server_pipe", 0666) == -1) {
        if (errno != EEXIST) {
            log_message("Error creating named pipe");
            exit(EXIT_FAILURE);
        }
    }

    fd_Server = open("/mnt/mta/server_pipe", O_RDONLY | O_NONBLOCK);
    if (fd_Server == -1) {
        log_message("Error opening named pipe");
        exit(EXIT_FAILURE);
    }

    int flags;
    flags = fcntl(fd_Server, F_GETFL); /* Fetch open files status flags */
    flags |= O_NONBLOCK; /* Enable O_NONBLOCK bit */
    fcntl(fd_Server, F_SETFL, flags); /* Update open files status flags */

    // Set up signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGHUP, signal_handler);

    log_message("Listening on /mnt/mta/server_pipe \n");
    // Handle incoming messages and manage miners
    while (true) 
    {
        TLV* tlv = readTlvFromPipe(fd_Server);
        if (tlv == NULL) continue;

        if (tlv->type == NEW_MINER) 
        {
            char miner_id_str[32];
            sscanf(tlv->value, "%s", miner_id_str);
            int miner_id = atoi(miner_id_str);
            char miner_pipe[32];
            sprintf(miner_pipe, "/mnt/mta/miner_%d", miner_id);
            miners_Count++;

            // Print new miner 
            log_message("Received connection request from %d, pipe name /mnt/mta/miner_%d \n", miner_id, miner_id);

            int fd_Miner = open(miner_pipe, O_WRONLY);
            if (fd_Miner == -1) 
            {
                log_message("Server: Error opening miner pipe");
            }

            TLV new_block_tlv;
            new_block_tlv.type = NEW_BLOCK;
            new_block_tlv.length = sizeof(Block_t);
            memcpy(new_block_tlv.value, next_block, sizeof(Block_t)); 

            writeTlvToPipe(fd_Miner, &new_block_tlv);
            close(fd_Miner);


        } 
        else if (tlv->type == NEW_BLOCK) 
        {
            Block_t* temp_Block = (Block_t*)tlv->value;

            if (verify_block(current_block, temp_Block)) 
            {
                log_message(ANSI_COLOR_YELLOW "Server:" ANSI_COLOR_RESET "New block added by %d, attributes: height:(%d), timestamp:(%d), hash:(0x%x), prev-hash:(0x%x), difficulty:(%d), nonce:(%d)\n"
                , temp_Block->relayed_by, temp_Block->height, temp_Block->timestamp, temp_Block->hash, temp_Block->prev_hash, temp_Block->difficulty, temp_Block->nonce);
                
                // Save block mined (temp) to be current, for new round 
                free(current_block);
                current_block = (Block_t*)malloc(sizeof(Block_t));
                current_block->difficulty = temp_Block->difficulty;
                current_block->height = temp_Block->height;
                current_block->prev_hash = temp_Block->prev_hash;
                current_block->hash = temp_Block->hash;


                // Prepare next block
                next_block = (Block_t*)malloc(sizeof(Block_t));
                next_block->height = temp_Block->height;
                next_block->hash = temp_Block->hash;
                next_block->prev_hash = temp_Block->prev_hash;
                next_block->difficulty = temp_Block->difficulty;
                next_block->nonce = temp_Block->nonce;
                next_block->relayed_by = temp_Block->relayed_by;
                next_block->timestamp = temp_Block->timestamp;

                // Broadcast new block 
                TLV new_block_tlv;
                new_block_tlv.type = NEW_BLOCK;
                new_block_tlv.length = sizeof(Block_t);
                memcpy(new_block_tlv.value, next_block, sizeof(Block_t));

                for (int i = 1; i <= miners_Count; i++) 
                {
                    char miner_pipe[32];
                    sprintf(miner_pipe, "/mnt/mta/miner_%d", i);
                    int fd_Miner = open(miner_pipe, O_WRONLY | O_NONBLOCK);
                     if (fd_Miner == -1) 
                        {
                            if (errno == ENXIO) {
                                // The pipe is not open on the other end (miner has stopped)
                                log_message("Miner pipe is not open, skipping...\n");
                                continue;
                            } else {
                                // Handle other errors
                                log_message("Server: Error opening miner pipe");
                                continue;
                            }
                        }
                    writeTlvToPipe(fd_Miner, &new_block_tlv);
                    close(fd_Miner);
                }

            }
        }

        free(tlv);
    }

    cleanup_pipes();
    if (fd_Server != -1) {
        close(fd_Server);
    }

    if(next_block == NULL)
    {
        free(next_block);
    }
    if(current_block == NULL)
        {
            free(current_block);
        }

    return 0;
}

Block_t* Initialize_genesis_block(int diff) {
    Block_t* genesis = (Block_t*)malloc(sizeof(Block_t));
    genesis->height = 0;
    genesis->timestamp = (int)time(NULL);
    genesis->hash = 0x12345678;
    genesis->prev_hash = 0;
    genesis->difficulty = diff;
    genesis->nonce = 0;
    genesis->relayed_by = 0;
    return genesis;
}

bool verify_difficulty(unsigned int hash, int diff) {
    int mask = -1 << (32 - diff);
    return !(mask & hash);
}

bool verify_block(Block_t* curr, Block_t* next) {
    if (!verify_difficulty(next->hash, curr->difficulty)) {
        log_message(ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET "Block's hash doesn't meet the difficulty requirement (block #%d / miner #%d)\n", next->height, next->relayed_by);
        return false;
    }
    if (next->height != curr->height + 1) {
        log_message(ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET "Block's height is out of order (block #%d / miner #%d)\n", next->height, next->relayed_by);
        log_message("Block's height: %d, Miner's block height: %d\n", curr->height, next->height);
        return false;
    }
    if (next->prev_hash != curr->hash) {
        log_message(ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET "Block's previous hash isn't suitable to current block's hash (block #%d / miner #%d)\n", next->height, next->relayed_by);
        log_message("Block's hash: 0x%x, Miner's block prev hash: 0x%x\n", curr->hash, next->prev_hash);
        return false;
    }
    if (next->hash != calc_hash(next)) {
        log_message(ANSI_COLOR_RED "ERROR: " ANSI_COLOR_RESET "Block's hash isn't suitable to calculated hash (block #%d / miner #%d)\n", next->height, next->relayed_by);
        return false;
    }
    return true;
}

unsigned int calc_hash(Block_t* block) {
    char input[MAX];
    snprintf(input, sizeof(input), "%d%d%u%d%d", block->height, block->timestamp, block->prev_hash, block->nonce, block->relayed_by);
    return (unsigned int)crc32(0, (const Bytef*)input, strlen(input));
}

void print_block(Block_t* block) {
    log_message(ANSI_COLOR_CYAN "Server: " ANSI_COLOR_RESET "New block added by %d, attributes: ", block->relayed_by);
    log_message("Height:(%d), ", block->height);
    log_message("Timestamp:(%d), ", block->timestamp);
    log_message("Hash:(0x%x), ", block->hash);
    log_message("Prev Hash:(0x%x), ", block->prev_hash);
    log_message("Difficulty:(%d), ", block->difficulty);
    log_message("Nonce:(%d)\n", block->nonce);
}

// Deserialize TLV from pipe
TLV* readTlvFromPipe(int pipeReadEnd) {
    TLV* tlv = (TLV*)malloc(sizeof(TLV));
    if (!tlv) {
        log_message("malloc failed");
        return NULL;
    }

    ssize_t len = read(pipeReadEnd, tlv, sizeof(tlv->type) + sizeof(tlv->length));

    if (len != sizeof(tlv->type) + sizeof(tlv->length)) {
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("Error reading TLV header");
        }
        free(tlv);
        return NULL;
    }

    len = read(pipeReadEnd, tlv->value, tlv->length);
    if (len != tlv->length) {
        if (len < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            log_message("Error reading TLV value");
        }
        free(tlv);
        return NULL;
    }

    return tlv;
}

// Serialize TLV to pipe
void writeTlvToPipe(int pipeWriteEnd, TLV* tlv) {
    int len = 0;

    len = write(pipeWriteEnd, (void*)tlv, sizeof(tlv->type) + sizeof(tlv->length) + tlv->length);
    if (len < 0) { log_message("type + length + value"); return; }
}

void cleanup_pipes() {
    DIR* dir = opendir("/mnt/mta");
    if (dir == NULL) {
        log_message("Error opening /mnt/mta directory");
        return;
    }
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "miner_", 6) == 0 || strcmp(entry->d_name, "server_pipe") == 0) {
            char filepath[256];
            strncpy(filepath, "/mnt/mta/", sizeof(filepath) - 1);
            strncat(filepath, entry->d_name, sizeof(filepath) - strlen(filepath) - 1);
            unlink(filepath);
        }
    }
    closedir(dir);
}


// Signal handler
void signal_handler(int signum) {
    log_message("Received signal %d, cleaning up and exiting...\n", signum);
    cleanup_pipes();
    if (fd_Server != -1) {
        close(fd_Server);
    }
    exit(signum);
}

// Read difficulty from configuration file
int read_difficulty_from_file(const char* filepath) {
    FILE* file = fopen(filepath, "r");
    if (file == NULL) {
        log_message("Error opening configuration file");
        exit(EXIT_FAILURE);
    }

    int difficulty = -1;
    char line[MAX];
    while (fgets(line, sizeof(line), file)) {
        if (sscanf(line, "DIFFICULTY=%d", &difficulty) == 1) {
            break;
        }
    }

    fclose(file);
    if (difficulty == -1) {
        fprintf(stderr, "Error reading difficulty from configuration file\n");
        exit(EXIT_FAILURE);
    }

    return difficulty;
}
