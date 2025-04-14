#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h> // Include for getuid() and getgid()
#include <time.h>   // Include for time()


// #include "wfs.h"

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

const int MAXIMUM_OFFSES = (int) BLOCK_SIZE / sizeof(off_t);
// const int MAXIMUM_OFFSES = 0;

char Disks[20][100];
char Disk[100];

struct wfs_sb superblock;

struct wfs_sb get_superblock(int fd) {
    struct wfs_sb superblock;

    if (lseek(fd, 0, SEEK_SET) == -1) {
        perror("error in lseek");
        exit(EXIT_FAILURE);
    }

    if (read(fd, &superblock, sizeof(superblock)) != sizeof(superblock)) {
        perror("error in read superblock");
        exit(EXIT_FAILURE);
    }

    return superblock;
}

void get_inode_bitmap(int fd, void* buffer) {
    if (lseek(fd, superblock.i_bitmap_ptr, SEEK_SET) == -1) {
        perror("error in lseek inode bitmap");
        exit(EXIT_FAILURE);
    }

    size_t size = (superblock.num_inodes + 7) / 8;
    if (read(fd, buffer, size) != size) {
        perror("error in read inode bitmap");
        exit(EXIT_FAILURE);
    }
}

void get_data_block_bitmap(int fd, void* buffer) {
    if (lseek(fd, superblock.d_bitmap_ptr, SEEK_SET) == -1) {
        perror("error in lseek data block bitmap");
        exit(EXIT_FAILURE);
    }

    size_t size = (superblock.num_data_blocks + 7) / 8;
    if (read(fd, buffer, size) != size) {
        perror("error in read data block bitmap");
        exit(EXIT_FAILURE);
    }
}

void print_data_block_bitmap(int fd) {
    size_t data_block_bitmap_size = (superblock.num_data_blocks + 7) / 8;
    char* data_block_bitmap = (char*)malloc(data_block_bitmap_size);
    get_data_block_bitmap(fd, data_block_bitmap);

    printf("Data block bitmap:\n");
    for (int i = 0; i < superblock.num_data_blocks; i++) {
        if (i % 8 == 0) {
            printf(" ");
        }
        printf("%d", (data_block_bitmap[i / 8] & (1 << (i % 8))) ? 1 : 0);
    }
    printf("\n");

    free(data_block_bitmap);
}

void print_inode_bitmap(int fd) {
    size_t inode_bitmap_size = (superblock.num_inodes + 7) / 8;
    char* inode_bitmap = (char*)malloc(inode_bitmap_size);
    get_inode_bitmap(fd, inode_bitmap);

    printf("Inode bitmap:\n");
    for (int i = 0; i < superblock.num_inodes; i++) {
        if (i % 8 == 0) {
            printf(" ");
        }
        printf("%d", (inode_bitmap[i / 8] & (1 << (i % 8))) ? 1 : 0);
    }
    printf("\n");

    free(inode_bitmap);
}

off_t allocate_datablock(int fd) {
    size_t data_block_bitmap_size = (superblock.num_data_blocks + 7) / 8;
    char* data_block_bitmap = (char*)malloc(data_block_bitmap_size);
    get_data_block_bitmap(fd, data_block_bitmap);


    int free_data_block = -1;
    for (int i = 0; i < superblock.num_data_blocks; i++) {
        if (!(data_block_bitmap[i / 8] & (1 << (i % 8)))) {
            free_data_block = i;
            data_block_bitmap[i / 8] |= (1 << (i % 8));
            break;
        }
    }

    if (free_data_block == -1) {
        fprintf(stderr, "error: no free data block\n");
        return -1;
    }

    // zero out the data block
    char buffer[BLOCK_SIZE];
    memset(buffer, 0, sizeof(buffer));
    off_t offset = superblock.d_blocks_ptr + free_data_block * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("error in lseek data block");
        free(data_block_bitmap);
        exit(EXIT_FAILURE);
    }

    if (write(fd, buffer, sizeof(buffer)) == -1) {
        perror("error in write data block1");
        free(data_block_bitmap);
        exit(EXIT_FAILURE);
    }

    // Write data block bitmap back to disk
    if (lseek(fd, superblock.d_bitmap_ptr, SEEK_SET) == -1) {
        perror("error in lseek data block bitmap");
        free(data_block_bitmap);
        exit(EXIT_FAILURE);
    }

    if (write(fd, data_block_bitmap, data_block_bitmap_size) != data_block_bitmap_size) {
        perror("error in write data block bitmap");
        free(data_block_bitmap);
        exit(EXIT_FAILURE);
    }

    free(data_block_bitmap);

    return superblock.d_blocks_ptr + free_data_block * BLOCK_SIZE;

}

struct wfs_inode fetch_inode(int fd, int inode_num) {
    struct wfs_inode inode;

    off_t offset = superblock.i_blocks_ptr + inode_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("error in lseek inode");
        exit(EXIT_FAILURE);
    }

    if (read(fd, &inode, sizeof(inode)) != sizeof(inode)) {
        perror("error in read inode");
        exit(EXIT_FAILURE);
    }

    return inode;
}

//TODO MABYE ADD RAID
int DEP_find_dentry_in_directory(int fd, int parent_inode_num, char name[MAX_NAME]) {
    struct wfs_inode inode = fetch_inode(fd, parent_inode_num);

    int num_dentries = 0;
    for (int i = 0; i < D_BLOCK && inode.blocks[i] != 0; i++) {
        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek directory entry");
            exit(EXIT_FAILURE);
        }

        struct wfs_dentry entry;
        while (read(fd, &entry, sizeof(entry)) == sizeof(entry)) {
            if (strcmp(entry.name, name) == 0) {
                return entry.num;
            }
        }
    }

    return -1;
}
//use read to get the dentry
int find_dentry_in_directory(int fd,int parent_inode_num, char name[MAX_NAME]) {
    // read parent node
    off_t parent_size = getInodeSize(fd, parent_inode_num);
    char* buffer = (char*)malloc(parent_size);
    readDiskByInode(parent_inode_num, buffer, parent_size, 0);
        
    // parse dentry array
    struct wfs_dentry* dentries = (struct wfs_dentry*)buffer;
    int num_dentries = parent_size / sizeof(struct wfs_dentry);
    for (int i = 0; i < num_dentries; i++) {
        if (strcmp(dentries[i].name, name) == 0) {
            return dentries[i].num;
        }
    }
    return -1;
}

int DEP_get_inodeNum_by_path(int fd, const char* path) {
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");


    int current_inode_number = 0;

    while (token != NULL) {
        struct wfs_inode inode = fetch_inode(fd, current_inode_number);
        // if (!S_ISDIR(inode.mode)) {
        //     fprintf(stderr, "error: %s is not a directory\n", token);
        //     return -1;
        // }
        current_inode_number = DEP_find_dentry_in_directory(fd, current_inode_number, token);
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    free(path_copy);


    return current_inode_number;
}

int get_inodeNum_by_path(const char* path) {
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");


    int current_inode_number = 0;
    //tokinize path
    //search all images for the file
    //return inode number
    //if no inode return
    //repeat for new inode

    while (token != NULL) {
        current_inode_number = find_dentry_in_directory(current_inode_number, token);
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    free(path_copy);

}

int writeExistingInodeToDisk(int fd, struct wfs_inode inode) {

    // Write inode to disk
    off_t offset = superblock.i_blocks_ptr + (inode.num * BLOCK_SIZE);
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("error in lseek inode");
        exit(EXIT_FAILURE);
    }

    if (write(fd, &inode, sizeof(inode)) != sizeof(inode)) {
        perror("error in write inode");
        exit(EXIT_FAILURE);
    }

    return inode.num;
}

int writeInodeToDisk(int fd, struct wfs_inode inode) {

    //find free inode
    size_t inode_bitmap_size = (superblock.num_inodes + 7) / 8;
    char* inode_bitmap = (char*)malloc(inode_bitmap_size);
    get_inode_bitmap(fd, inode_bitmap);

    int free_inode = -1;
    for (int i = 0; i < superblock.num_inodes; i++) {
        if (!(inode_bitmap[i / 8] & (1 << (i % 8)))) {
            free_inode = i;
            inode_bitmap[i / 8] |= (1 << (i % 8));
            break;
        }
    }

    if (free_inode == -1) {
        fprintf(stderr, "error: no free inode\n");
        return -1;
    }

    inode.num = free_inode;

    // Write inode bitmap back to disk
    if (lseek(fd, superblock.i_bitmap_ptr, SEEK_SET) == -1) {
        perror("error in lseek inode bitmap");
        free(inode_bitmap);
        exit(EXIT_FAILURE);
    }

    if (write(fd, inode_bitmap, inode_bitmap_size) != inode_bitmap_size) {
        perror("error in write inode bitmap");
        free(inode_bitmap);
        exit(EXIT_FAILURE);
    }

    free(inode_bitmap);
    return writeExistingInodeToDisk(fd, inode);
}

//TODO fix -1 bug after 31 directories
int make_directory(int fd, const char* path, mode_t mode) {
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");

    // if (get_inodeNum_by_path(fd, path) != -1) {
    //     fprintf(stderr, "error: %s already exists\n", path);
    //     return -1;
    // }  


    int current_inode_number = 0;

    while (nextToken != NULL) {
        struct wfs_inode inode = fetch_inode(fd, current_inode_number);
        // if (!S_ISDIR(inode.mode)) {
        //     fprintf(stderr, "error: %s is not a directory\n", token);
        //     return -1;
        // }
        current_inode_number = DEP_find_dentry_in_directory(fd, current_inode_number, token);
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    //create new inode
    struct wfs_inode new_inode;
    printf("%d", mode);
    new_inode.mode = 16877;
    new_inode.uid = 1200;
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 1;
    new_inode.atim = time(NULL);
    new_inode.mtim = time(NULL);
    new_inode.ctim = time(NULL);
    memset(new_inode.blocks, 0, sizeof(new_inode.blocks));

    int new_inode_num = writeInodeToDisk(fd, new_inode);

    //create new dentry
    struct wfs_dentry new_dentry;
    strcpy(new_dentry.name, token);
    new_dentry.num = new_inode_num;
    struct wfs_inode parent_inode = fetch_inode(fd, current_inode_number);
    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode.blocks[i] == 0) {
            //create new data block
            char buffer[BLOCK_SIZE];
            memset(buffer, 0, sizeof(buffer));
            off_t dataBlock_offset = allocate_datablock(fd);
            if (dataBlock_offset == -1) {
                perror("no free data block");
                return -1;
            }
            if (lseek(fd, dataBlock_offset, SEEK_SET) == -1) {
                perror("error in lseek data block");
                exit(EXIT_FAILURE);
            }

            if (write(fd, buffer, sizeof(buffer)) == -1) {
                perror("error in write data block1");
                exit(EXIT_FAILURE);
            }
            parent_inode.blocks[i] = dataBlock_offset;
            off_t offset = superblock.i_blocks_ptr + current_inode_number * BLOCK_SIZE;
            writeExistingInodeToDisk(fd, parent_inode);

            //create new dentry
            off_t dentry_offset = dataBlock_offset;
            if (lseek(fd, dentry_offset, SEEK_SET) == -1) {
                perror("error in lseek dentry");
                exit(EXIT_FAILURE);
            }

            if (write(fd, &new_dentry, sizeof(new_dentry)) == -1) {
                perror("error in write dentry");
                exit(EXIT_FAILURE);
            }

            break;
        }
        else {
            off_t offset = parent_inode.blocks[i];
            if (lseek(fd, offset, SEEK_SET) == -1) {
                perror("error in lseek directory entry");
                exit(EXIT_FAILURE);
            }

            struct wfs_dentry entry;
            int count = 0;
            while (read(fd, &entry, sizeof(entry)) == sizeof(entry) && count < MAX_DENTRY_AMOUNT) {
                if (entry.num == 0) {
                    off_t dentry_offset = offset + count * sizeof(entry);
                    if (lseek(fd, dentry_offset, SEEK_SET) == -1) {
                        perror("error in lseek dentry");
                        exit(EXIT_FAILURE);
                    }

                    if (write(fd, &new_dentry, sizeof(new_dentry)) == -1) {
                        perror("error in write dentry");
                        exit(EXIT_FAILURE);
                    }
                    return 0;
                }
                count++;
            }
        }
    }

    free(path_copy);

    return 0;
}

int make_file(int fd, const char* path) {
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");

    int current_inode_number = 0;

    // Traverse to the parent directory of the new file
    while (nextToken != NULL) {
        struct wfs_inode inode = fetch_inode(fd, current_inode_number);

        current_inode_number = DEP_find_dentry_in_directory(fd, current_inode_number, token);
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    // Create new inode for the file
    struct wfs_inode new_inode;
    new_inode.mode = 33261; // Regular file mode
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 1;
    new_inode.atim = time(NULL);
    new_inode.mtim = time(NULL);
    new_inode.ctim = time(NULL);
    memset(new_inode.blocks, 0, sizeof(new_inode.blocks));

    int new_inode_num = writeInodeToDisk(fd, new_inode);

    // Create new directory entry for the file
    struct wfs_dentry new_dentry;
    strcpy(new_dentry.name, token);
    new_dentry.num = new_inode_num;

    // Add the new entry to the parent directory
    struct wfs_inode parent_inode = fetch_inode(fd, current_inode_number);

    for (int i = 0; i < D_BLOCK; i++) {
        if (parent_inode.blocks[i] == 0) {
            // Create new data block for parent directory
            char buffer[BLOCK_SIZE];
            memset(buffer, 0, sizeof(buffer));
            off_t dataBlock_offset = allocate_datablock(fd);
            if (dataBlock_offset == -1) {
                perror("no free data block");
                free(path_copy);
                return -1;
            }

            if (lseek(fd, dataBlock_offset, SEEK_SET) == -1) {
                perror("error in lseek data block");
                free(path_copy);
                exit(EXIT_FAILURE);
            }

            if (write(fd, buffer, sizeof(buffer)) == -1) {
                perror("error in write data block2");
                free(path_copy);
                exit(EXIT_FAILURE);
            }

            parent_inode.blocks[i] = dataBlock_offset;
            writeExistingInodeToDisk(fd, parent_inode);

            if (lseek(fd, dataBlock_offset, SEEK_SET) == -1) {
                perror("error in lseek dentry");
                free(path_copy);
                exit(EXIT_FAILURE);
            }

            if (write(fd, &new_dentry, sizeof(new_dentry)) == -1) {
                perror("error in write dentry");
                free(path_copy);
                exit(EXIT_FAILURE);
            }

            break;
        }
        else {
            off_t offset = parent_inode.blocks[i];
            if (lseek(fd, offset, SEEK_SET) == -1) {
                perror("error in lseek directory entry");
                free(path_copy);
                exit(EXIT_FAILURE);
            }

            struct wfs_dentry entry;
            int count = 0;

            while (read(fd, &entry, sizeof(entry)) == sizeof(entry) && count < MAX_DENTRY_AMOUNT) {
                if (entry.num == 0) {
                    off_t dentry_offset = offset + count * sizeof(entry);
                    if (lseek(fd, dentry_offset, SEEK_SET) == -1) {
                        perror("error in lseek dentry");
                        free(path_copy);
                        exit(EXIT_FAILURE);
                    }

                    if (write(fd, &new_dentry, sizeof(new_dentry)) == -1) {
                        perror("error in write dentry");
                        free(path_copy);
                        exit(EXIT_FAILURE);
                    }
                    free(path_copy);
                    return new_inode_num;
                }
                count++;
            }
        }
    }

    free(path_copy);
    return new_inode_num;
}

void print_inode(struct wfs_inode inode) {
    printf("Inode number: %d\n", inode.num);
    printf("Mode: %o\n", inode.mode);
    printf("User ID: %d\n", inode.uid);
    printf("Group ID: %d\n", inode.gid);
    printf("Size: %ld bytes\n", inode.size);
    printf("Number of links: %d\n", inode.nlinks);
    printf("Last access time: %s", ctime(&inode.atim));
    printf("Last modification time: %s", ctime(&inode.mtim));
    printf("Last status change time: %s", ctime(&inode.ctim));

    for (int i = 0; i < N_BLOCKS; i++) {
        printf("Block %d: %lld\n", i, inode.blocks[i]);
    }
}

void print_Directory(int fd, int inode_num) {
    struct wfs_inode inode = fetch_inode(fd, inode_num);
    // inode.num = inode_num;
    for (int i = 0; (i < D_BLOCK) && (inode.blocks[i] != 0); i++) {
        off_t offset = inode.blocks[i];
        printf("Offset: %ld\n", inode.blocks[i]);
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek directory entry");
            exit(EXIT_FAILURE);
        }
        struct wfs_dentry entry;
        int count = 0;
        while (read(fd, &entry, sizeof(entry)) == sizeof(entry)) {
            if (count == MAX_DENTRY_AMOUNT) {
                break;
            }
            count++;
            if (entry.num == 0) {
                continue;
            }
            printf("\nName: %s\n", entry.name);
            printf("Inode number: %d\n", entry.num);
        }
    }
}

void print_Block(int fd, int block_num) {
    off_t offset = superblock.d_blocks_ptr + block_num * BLOCK_SIZE;
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("error in lseek data block");
        exit(EXIT_FAILURE);
    }

    char buffer[BLOCK_SIZE];
    if (read(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
        perror("error in read data block");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < BLOCK_SIZE; i++) {
        if (i % 16 == 0) {
            printf("\n");
        }
        printf("%02X ", (unsigned char)buffer[i]);
    }
    printf("\n");
}

void print_indirect_block(int fd, off_t offset) {
    if (lseek(fd, offset, SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        exit(EXIT_FAILURE);
    }

    off_t indirect_block_offsets[MAXIMUM_OFFSES];
    if (read(fd, indirect_block_offsets, sizeof(indirect_block_offsets)) != sizeof(indirect_block_offsets)) {
        perror("error in read indirect block");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {
        // printf("Offset %d: %ld\n", i, indirect_block_offsets[i]);
    }
}

int writeFile(int fd, const char* path, const char* buffer, size_t size, off_t wanted_offset) {
    int inode_num = DEP_get_inodeNum_by_path(fd, path);
    if (inode_num == -1) {
        fprintf(stderr, "error: %s not found\n", path);
        return -1;
    }

    struct wfs_inode inode = fetch_inode(fd, inode_num);
    if (!S_ISREG(inode.mode)) {
        fprintf(stderr, "error: %s is not a regular file\n", path);
        return -1;
    }

    // Write data to the file
    size_t bytes_written = 0;
    size_t offset_written = 0;
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode.blocks[i] == 0) {
            // Allocate new data block
            off_t data_block_offset = allocate_datablock(fd);
            if (data_block_offset == -1) {
                perror("no free data block");
                return -1;
            }

            inode.blocks[i] = data_block_offset;
            writeExistingInodeToDisk(fd, inode);
        }

        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek data block");
            return -1;
        }

        size_t position = 0;
        if (offset_written < wanted_offset) {
            if (wanted_offset - offset_written >= BLOCK_SIZE) {
                offset_written += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_written, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_written;
            offset_written = wanted_offset;
        }

        size_t bytes_to_write = size - bytes_written;
        if (bytes_to_write > BLOCK_SIZE - position) {
            bytes_to_write = BLOCK_SIZE - position;
        }

        if (write(fd, buffer + bytes_written, bytes_to_write) != bytes_to_write) {
            perror("error in write data block3");
            return -1;
        }

        bytes_written += bytes_to_write;
        if (bytes_written == size) {
            return size;
        }

    }

    printf("\nbefore indirect block\n");
    printf("bytes_written: %ld\n", bytes_written);
    printf("offset_written: %ld\n", offset_written);   
    printf("after indirect block\n");


    //allocate indirect block
    if (inode.blocks[D_BLOCK] == 0) {
        off_t data_block_offset = allocate_datablock(fd);
        if (data_block_offset == -1) {
            perror("no free data block");
            return -1;
        }
        inode.blocks[D_BLOCK] = data_block_offset;
        writeExistingInodeToDisk(fd, inode);
    }

    int count = 0;
    off_t indirect_block_offsets[MAXIMUM_OFFSES];

    if (lseek(fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        return -1;
    }


    for (int i = 0; i < MAXIMUM_OFFSES; i++){
        // if (read(fd, &indirect_block_offsets[i], sizeof(off_t)) != sizeof(off_t)) {
        //     perror("error in read indirect block");
        //     return -1;
        // }
        if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        read(fd, &indirect_block_offsets[i], sizeof(off_t));
        printf("indirect_block_offsets[%d]: %ld\n", i, indirect_block_offsets[i]);

        if (indirect_block_offsets[i] == 0) {
            off_t data_block_offset = allocate_datablock(fd);
            printf("%ld\n", data_block_offset);
            if (data_block_offset == -1) {
                perror("no free data block");
                return -1;
            }
            indirect_block_offsets[i] = data_block_offset;
            printf("changeing to %ld\n", indirect_block_offsets[i]);

            if (lseek(fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1) {
                perror("error in lseek indirect block");
                return -1;
            }

            if (write(fd, &data_block_offset, sizeof(off_t)) != sizeof(off_t)) {
                perror("error in write indirect block");
                return -1;
            }
        }

        if (lseek(fd, indirect_block_offsets[i], SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        size_t position = 0;

        if (offset_written < wanted_offset) {
            if (wanted_offset - offset_written >= BLOCK_SIZE) {
                offset_written += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_written, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_written;
            offset_written = wanted_offset;
        }

        size_t bytes_to_write = size - bytes_written;

        if (bytes_to_write > BLOCK_SIZE - position) {
            bytes_to_write = BLOCK_SIZE - position;
        }

        if (write(fd, buffer + bytes_written, bytes_to_write) != bytes_to_write) {
            perror("error in write data block4");
            return -1;
        }

        bytes_written += bytes_to_write;
        if (bytes_written == size) {
            printf("bytes_written: %ld\n", bytes_written);
            printf("offset_written: %ld\n", offset_written);            
            return size;
        }


    }
    printf("Too much data written\n");
    return 0;
}

off_t getInodeSize(int fd, int inode_num) {
    struct wfs_inode inode = fetch_inode(fd, inode_num);
    int num_of_blocks = 0;

    for (int i = 0; i < D_BLOCK; i++) {
        if (inode.blocks[i] == 0) {
            return num_of_blocks * BLOCK_SIZE;
        }
        num_of_blocks++;
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {

        off_t offset;

        if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        read(fd, &offset, sizeof(off_t));

        if (offset == 0) {
            return num_of_blocks * BLOCK_SIZE;
        }
        num_of_blocks++;
    }
    return num_of_blocks * BLOCK_SIZE;
}

int DEP_readFileByInode(int fd, int inode_num, char* buffer, size_t size, off_t wanted_offset) {

    
    struct wfs_inode inode = fetch_inode(fd, inode_num);
    if (!S_ISREG(inode.mode)) {
        fprintf(stderr, "error: is not a regular file\n");
        return -1;
    }

    // Read data from the file
    size_t bytes_read = 0;
    size_t offset_read = 0;
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode.blocks[i] == 0) {
            printf("End of file\n");
            return 0;
        }

        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek data block");
            return -1;
        }

        size_t position = 0;
        if (offset_read < wanted_offset) {
            if (wanted_offset - offset_read >= BLOCK_SIZE) {
                offset_read += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_read, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_read;
            offset_read = wanted_offset;
        }

        size_t bytes_to_read = size - bytes_read;
        if (bytes_to_read > BLOCK_SIZE - position) {
            bytes_to_read = BLOCK_SIZE - position;
        }

        if (read(fd, buffer + bytes_read, bytes_to_read) != bytes_to_read) {
            perror("error in read data block3");
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            return size;
        }
    }

    // printf("____%s_____\n\n", buffer);

    printf("\nbefore indirect block\n");
    printf("bytes_read: %ld\n", bytes_read);
    printf("offset_read: %ld\n", offset_read);
    printf("after indirect block\n");

    // allocate indirect block
    if (inode.blocks[D_BLOCK] == 0) {
        printf("End of file\n");
        return 0;
    }

    int count = 0;
    off_t indirect_block_offsets[MAXIMUM_OFFSES];

    if (lseek(fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        return -1;
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {
        // printf("_%d__%s_____\n\n", i, buffer);
        if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        read(fd, &indirect_block_offsets[i], sizeof(off_t));
        printf("indirect_block_offsets[%d]: %ld\n", i, indirect_block_offsets[i]);

        if (indirect_block_offsets[i] == 0) {
            printf("End of file\n");
            return 0;
        }

        if (lseek(fd, indirect_block_offsets[i], SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        size_t position = 0;

        if (offset_read < wanted_offset) {
            if (wanted_offset - offset_read >= BLOCK_SIZE) {
                offset_read += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_read, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_read;
            offset_read = wanted_offset;
        }

        size_t bytes_to_read = size - bytes_read;

        if (bytes_to_read > BLOCK_SIZE - position) {
            bytes_to_read = BLOCK_SIZE - position;
        }

        if (read(fd, buffer + bytes_read, bytes_to_read) != bytes_to_read) {
            perror("error in read data block4");
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            printf("bytes_read: %ld\n", bytes_read);
            printf("offset_read: %ld\n", offset_read);
            return size;
        }
    }
    printf("Too much data read\n");
    return 0;
}

int readDiskByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset) {

    
    struct wfs_inode inode = fetch_inode(fd, inode_num);
    if (!S_ISREG(inode.mode)) {
        fprintf(stderr, "error: is not a regular file\n");
        return -1;
    }

    // Read data from the file
    size_t bytes_read = 0;
    size_t offset_read = 0;
    for (int i = 0; i < D_BLOCK; i++) {
        if (inode.blocks[i] == 0) {
            printf("End of file\n");
            return 0;
        }

        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek data block");
            return -1;
        }

        size_t position = 0;
        if (offset_read < wanted_offset) {
            if (wanted_offset - offset_read >= BLOCK_SIZE) {
                offset_read += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_read, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_read;
            offset_read = wanted_offset;
        }

        size_t bytes_to_read = size - bytes_read;
        if (bytes_to_read > BLOCK_SIZE - position) {
            bytes_to_read = BLOCK_SIZE - position;
        }

        if (read(fd, buffer + bytes_read, bytes_to_read) != bytes_to_read) {
            perror("error in read data block3");
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            return size;
        }
    }

    // printf("____%s_____\n\n", buffer);

    printf("\nbefore indirect block\n");
    printf("bytes_read: %ld\n", bytes_read);
    printf("offset_read: %ld\n", offset_read);
    printf("after indirect block\n");

    // allocate indirect block
    if (inode.blocks[D_BLOCK] == 0) {
        printf("End of file\n");
        return 0;
    }

    int count = 0;
    off_t indirect_block_offsets[MAXIMUM_OFFSES];

    if (lseek(fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        return -1;
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {
        // printf("_%d__%s_____\n\n", i, buffer);
        if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        read(fd, &indirect_block_offsets[i], sizeof(off_t));
        printf("indirect_block_offsets[%d]: %ld\n", i, indirect_block_offsets[i]);

        if (indirect_block_offsets[i] == 0) {
            printf("End of file\n");
            return 0;
        }

        if (lseek(fd, indirect_block_offsets[i], SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            return -1;
        }

        size_t position = 0;

        if (offset_read < wanted_offset) {
            if (wanted_offset - offset_read >= BLOCK_SIZE) {
                offset_read += BLOCK_SIZE;
                continue;
            }
            if (lseek(fd, wanted_offset - offset_read, SEEK_CUR) == -1) {
                perror("error in lseek data block");
                return -1;
            }
            position = wanted_offset - offset_read;
            offset_read = wanted_offset;
        }

        size_t bytes_to_read = size - bytes_read;

        if (bytes_to_read > BLOCK_SIZE - position) {
            bytes_to_read = BLOCK_SIZE - position;
        }

        if (read(fd, buffer + bytes_read, bytes_to_read) != bytes_to_read) {
            perror("error in read data block4");
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            printf("bytes_read: %ld\n", bytes_read);
            printf("offset_read: %ld\n", offset_read);
            return size;
        }
    }
    printf("Too much data read\n");
    return 0;
}

int readFile(int fd, const char* path, char* buffer, size_t size, off_t wanted_offset) {
    int inode_num = DEP_get_inodeNum_by_path(fd, path);
    if (inode_num == -1) {
        fprintf(stderr, "error: %s not found\n", path);
        return -1;
    }

    return DEP_readFileByInode(fd, inode_num, buffer, size, wanted_offset);
}

// Example usage
int main() {
    strcmp(Disks[0], "test-disk1.img");
    strcpy(Disk, Disks[0]);
    const char* disk_image = "test-disk1.img";
    int fd = open(disk_image, O_RDWR);
    if (fd == -1) {
        perror("error in open");
        return -1;
    }

    // Retrieve the superblock
    superblock = get_superblock(fd);

    make_directory(fd, "/test", 0);
    make_directory(fd, "/test/test2", 0);
    make_directory(fd, "/test/test2/test3", 0);

    make_directory(fd, "/apple", 0);
    make_directory(fd, "/banana", 0);
    make_file(fd, "/banana/penuts");
    printf("\n\n\n\n");
    
    int file_size = 9 * BLOCK_SIZE;
    char toWrite[file_size];
    for (int i = 0; i < file_size; i++) {
        if(i % BLOCK_SIZE == 0) {
            toWrite[i] = '_';
            printf("%c", 'a' + (i/BLOCK_SIZE));
        }
        else{
            toWrite[i] = 'a' + (i/BLOCK_SIZE);
        }
    }

    writeFile(fd, "/banana/penuts", toWrite, file_size, 0);

    for (int i = 0; i < file_size; i++) {
        if(i % BLOCK_SIZE == 0) {
            toWrite[i] = '_';
        }
        else{
            toWrite[i] = 'p' + (i/BLOCK_SIZE);
        }
    }

    printf("\n\n\n\n");
    writeFile(fd, "/banana/penuts", toWrite, file_size, file_size);
    printf("\n\n\n\n");

    // print_data_block_bitmap(fd);
    // print_Block(fd, 8);
    // print_indirect_block(fd, 8);
    // // create buffer
    char buffer[file_size * 2];
    readFile(fd, "/banana/penuts", buffer, file_size * 2 , 0);
    fflush(stdout);
    printf("----------");
    printf("Buffer: %s", buffer);
    printf("----------");
    fflush(stdout);


    // printf("Superblock retrieved: %d inodes, %d data blocks\n", superblock.disk_amount, superblock.disk_num);

    // Inodebitmap
    // size_t inode_bitmap_size = (superblock.num_inodes + 7) / 8;
    // char* inode_bitmap = (char*)malloc(inode_bitmap_size);
    // get_inode_bitmap(fd, inode_bitmap);

    // for (size_t i = 0; i < inode_bitmap_size; i++) {
    //     // printf("inode_bitmap[%zu]: 0x%02X\n", i, (unsigned char)inode_bitmap[i]);
    // }


    // Inode
    // struct wfs_inode inode = fetch_inode(fd, 0);
    // // print_inode(inode);

    // struct wfs_inode inode_by_path = fetch_inode(fd,get_inodeNum_by_path(fd, "/"));
    // print_inode(inode_by_path);

    // Create directory



    // make_directory(fd, "/apple");
    // make_directory(fd, "/banana");
    // make_directory(fd, "/mint");
    // make_directory(fd, "/penuts");
    // make_directory(fd, "/penuts");


        //Datablocksbitmap
    // size_t data_block_bitmap_size = (superblock.num_data_blocks + 7) / 8;
    // char* data_block_bitmap = (char*)malloc(data_block_bitmap_size);
    // get_data_block_bitmap(fd, data_block_bitmap);

    // for (size_t i = 0; i < data_block_bitmap_size; i++) {
    //     printf("data_block_bitmap[%zu]: 0x%02X\n", i, (unsigned char)data_block_bitmap[i]);
    // }



    // printf("\n\n\n\nTest:");
    //     fflush(stdout);
    // int inode_num = get_inodeNum_by_path(fd, "/test");
    // if (inode_num != -1) {
    //     struct wfs_inode inode = fetch_inode(fd, inode_num);
    //     print_inode(fetch_inode(fd, inode_num));
    // } else {
    //     printf("Directory not found\n");
    // }


    // Create inode



    close(fd);
    return 0;
}



// int writeRaid0ByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset) {
//     int number_of_disks = 2;
//     int diskNumber = 0;
//     int itteration = 0;

//     size_t bytes_written = 0;
//     size_t offset_written = 0;

//     int fd = open(Disk[diskNumber], O_RDWR);
//     struct wfs_inode inode = fetch_inode(fd, inode_num);


//     while (bytes_written < size &&  offset_written < wanted_offset) {
//         if( itteration < D_BLOCK){
//             off_t offset = inode.blocks[itteration];
//             if (lseek(fd, offset, SEEK_SET) == -1) {
//                 perror("error in lseek data block");
//                 return -1;
//             }

//             size_t bytes_to_write = size - bytes_written;
//             if (bytes_to_write > BLOCK_SIZE) {
//                 bytes_to_write = BLOCK_SIZE;
//             }

//             if (write(fd, buffer + offset_written, bytes_to_write) != bytes_to_write) {
//                 perror("error in write data block");
//                 return -1;
//             }

//             bytes_written += bytes_to_write;
//             offset_written += bytes_to_write;
//         }

//         if(diskNumber == number_of_disks - 1){
//             itteration++;
//         }
//         diskNumber = (diskNumber + 1) % number_of_disks;
        
//     }

// }