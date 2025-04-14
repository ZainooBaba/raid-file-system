#include <stdio.h>

#include <string.h>

#include <errno.h>

#include <sys/stat.h>

#include <unistd.h> // Include for getuid() and getgid()

#include <time.h>   // Include for time()


#include "wfs.h"


#include <stdio.h>

#include <unistd.h>

#include <fcntl.h>

#include <string.h>

#include <stdlib.h>

#include <errno.h>

#include <stdbool.h>



int Min(int a, int b);
int Max(int a, int b);

struct wfs_sb get_superblock(int fd);
void get_inode_bitmap(int fd, void* buffer);
void get_data_block_bitmap(int fd, void* buffer);
void print_data_block_bitmap(int fd);
void print_inode_bitmap(int fd);
off_t allocate_datablock(int fd);
int get_inodeNum_by_path(const char* path, int isNewFile);
struct wfs_inode fetch_inode(int fd, int inode_num);
off_t getInodeSize(int inode_num);

int readFileByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset);
int writeFileByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset);

int writeExistingInodeToDisk(int fd, struct wfs_inode inode);
int writeInodeToDisk(int fd, struct wfs_inode inode);
int writeNewInodeToDisk(struct wfs_inode inode);

int make_directory(const char* path, mode_t mode);

int Min(int a, int b) {
    return a < b ? a : b;
}

int Max(int a, int b) {
    return a > b ? a : b;
}

const int MAXIMUM_OFFSES = (int) BLOCK_SIZE / sizeof(off_t);
const int MAX_DIRECT_DATA = 7 * 512;
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
    fflush(stdout);
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
    fflush(stdout);
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

int get_inodeNum_by_path(const char* path, int isNewFile) {
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");

    int current_inode_number = 0;

    
    while ((nextToken != NULL && isNewFile) || (token != NULL && !isNewFile)) {
    //     //get the contents of the current directory
        off_t size = getInodeSize(current_inode_number);
        char buffer[size];

        readFileByInode(current_inode_number, buffer, size, 0);


        //get the inode number of the next directory
        off_t offset = 0;
        bool found = false;
        struct wfs_dentry entry;
        while (offset < size) {
            memcpy(&entry, buffer + offset, sizeof(entry));
            if (strcmp(entry.name, token) == 0 && entry.num != 0) {
                found = true;
                break;
            }
            offset += sizeof(entry);
        }

        printf("current_inode_number: %d\n", current_inode_number);
        if (!found) {
            printf("File not found %s\n", path);
            // free(path_copy);
            return -1;
        }
        
        current_inode_number = entry.num;

        //goes next token
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    free(path_copy);
    return current_inode_number;

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


//TODO make it work with raid 1v
off_t getInodeSize(int inode_num) {

    char* disk = Disks[0];
    int count = 0;
    off_t total_size = 0;
    while ((strcmp(disk, "\0") != 0  && superblock.raid_mode == 0) || (count == 0 && superblock.raid_mode == 1)) {
        // open disk
        disk = Disks[count];
        if(strcmp(disk, "\0") == 0) break;
        count++;
        int fd = open(disk, O_RDONLY);
        if (fd == -1) {
            perror("error in open");
            return -1;
        }

        // get inode
        struct wfs_inode inode = fetch_inode(fd, inode_num);
        close(fd);

        // check direct blocks
        int num_of_blocks = 0;
        for (int i = 0; i < D_BLOCK; i++) {
            if (inode.blocks[i] == 0) {
                break;
            }
            num_of_blocks++;
        }

        // check indirect blocks
        if(inode.blocks[D_BLOCK] != 0){
            fd = open(disk, O_RDONLY);
            for (int i = 0; i < MAXIMUM_OFFSES; i++) {
                off_t offset;
                if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
                    perror("error in lseek indirect block");
                    return -1;
                }
                read(fd, &offset, sizeof(off_t));

                if (offset == 0) {
                    break;
                }
                num_of_blocks++;
            }
            close(fd);
        }

        total_size += num_of_blocks * BLOCK_SIZE;

        // check next disk if needed
        fflush(stdout);
    }
    return total_size;
}


// int getAllDataBlocks(int inode_num, off_t* blocks) {
//     for(int i = 0; i < N_BLOCKS; i++) {
//         blocks[i] = 0;
//     }
//     struct wfs_inode inode = fetch_inode(fd, inode_num);
//     int num_of_blocks = 0;

//     for (int i = 0; i < D_BLOCK; i++) {
//         if (inode.blocks[i] == 0) {
//             return num_of_blocks;
//         }
//         blocks[num_of_blocks++] = inode.blocks[i];
//     }

//     for (int i = 0; i < MAXIMUM_OFFSES; i++) {
//         off_t offset;

//         if (lseek(fd, inode.blocks[D_BLOCK] + i * sizeof(off_t), SEEK_SET) == -1) {
//             perror("error in lseek indirect block");
//             return -1;
//         }

//         read(fd, &offset, sizeof(off_t));

//         if (offset == 0) {
//             return num_of_blocks;
//         }
//         blocks[num_of_blocks++] = offset;
//     }
//     return num_of_blocks;
// }

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
        return -ENOSPC;
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

int writeNewInodeToDisk(struct wfs_inode inode) {
    char* disk = Disks[0];
    int count = 0;
    off_t total_size = 0;
    int index = -1;

    while (strcmp(disk, "\0") != 0) {
        // open disk
        bool flag = false;
        int fd = open(disk, O_RDWR);
        if (fd == -1) {
            perror("error in open");
            return -1;
        }

        index = writeInodeToDisk(fd, inode);
        if (index == -1 || index == -ENOSPC) {
            close(fd);
            return index;
        }

        close(fd);
        disk = Disks[++count];
    }
    return index;
}


int readDiskByInode1(int fd, int inode_num, char* buffer, size_t size, off_t wanted_offset) {

    
struct wfs_inode inode = fetch_inode(fd, inode_num);
    // if (!S_ISREG(inode.mode)) {
    //     fprintf(stderr, "error: is not a regular file\n");
    //     return -1;
    // }

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

int readDiskByInode(const char* filepath, int inode_num, const char* buffer, size_t size, off_t wanted_offset) {
    if (size == 0) {
        return 0;
    }

    printf("_____readDiskByInode\n");

    // Open the file
    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        perror("error opening file");
        return -1;
    }

    // Fetch the inode
    struct wfs_inode inode = fetch_inode(fd, inode_num);

    // Initialize read tracking variables
    size_t bytes_read = 0;
    size_t offset_read = 0;

    // Direct blocks
    for (int i = 0; i < D_BLOCK; i++) {

        if (inode.blocks[i] == 0) {
            close(fd);
            off_t inodeSize = getInodeSize(inode_num);
            fd = open(filepath, O_RDWR);
            if (inodeSize >= MAX_DIRECT_DATA) {
                printf("moving to indirect blocks\n");
                break;
            }
            close(fd);
            return bytes_read;
        }

        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek data block");
            close(fd);
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
                close(fd);
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
            perror("error in read data block");
            close(fd);
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            close(fd);
            return size;
        }

        offset_read += BLOCK_SIZE;
    }

    // Indirect blocks
    if (inode.blocks[D_BLOCK] == 0) {
        close(fd);
        return bytes_read;
    }

    off_t indirect_block_offsets = 0;

    if (lseek(fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        close(fd);
        return -1;
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {
        lseek(fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET);
        read(fd, &indirect_block_offsets, sizeof(off_t));

        if (indirect_block_offsets == 0) {
            close(fd);
            return bytes_read;
        }

        if (lseek(fd, indirect_block_offsets, SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            close(fd);
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
                close(fd);
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
            perror("error in read data block");
            close(fd);
            return -1;
        }

        bytes_read += bytes_to_read;
        if (bytes_read == size) {
            close(fd);
            return size;
        }

        offset_read += BLOCK_SIZE;
    }

    printf("Too much data read\n");

    // Close the file
    close(fd);
    return 0;
}


int readFileByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset) {
    // RAID 0
    if (superblock.raid_mode == 0) {
        int number_of_disks = superblock.disk_amount;
        int diskNumber = 0;
        int iteration = 0;

        size_t bytes_read = 0;
        size_t offset_read = 0;

        while (bytes_read < size || offset_read < wanted_offset) {
            size_t bytes = 0;
            size_t offset = 0;

            if (offset_read < wanted_offset) {
                offset = Min(wanted_offset - offset_read, BLOCK_SIZE);
            }

            if (bytes_read < size) {
                bytes = Min(size - bytes_read, BLOCK_SIZE-offset);
            }

            if (readDiskByInode(Disks[diskNumber], inode_num, buffer + bytes_read, bytes, offset + (BLOCK_SIZE * iteration)) == -1) {
                 return -1;
            }

            offset_read += offset;
            bytes_read += bytes;

            diskNumber++;
            if (diskNumber == number_of_disks) {
                diskNumber = 0;
                iteration++;
            }
        }
    }

    // RAID 1
    if (superblock.raid_mode == 1) {
        int fd = open(Disks[0], O_RDWR);
        if (readDiskByInode1(fd, inode_num, buffer, size, wanted_offset) == -1) {
            close(fd);
            return -1;
        }
        close(fd);
        // return readDiskByInode(Disk[0], inode_num, buffer, size, wanted_offset);

    }

    // TODO: implement RAID 1v
}

int writeDiskByInode1(int fd, int inode_num, const char* buffer, size_t size, off_t wanted_offset) {

    if (size == 0) {
        return 0;
    }
    printf("_____writeDiskByInode\n");
    struct wfs_inode inode = fetch_inode(fd, inode_num);
    // if (!S_ISREG(inode.mode)) {
    //     fprintf(stderr, "error: %s is not a regular file\n", path);
    //     return -1;
    // }

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

        offset_written += BLOCK_SIZE;
    }


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
        lseek(fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET);
        read(fd, &indirect_block_offsets[i], sizeof(off_t));

        if (indirect_block_offsets[i] == 0) {
            off_t data_block_offset = allocate_datablock(fd);
            if (data_block_offset == -1) {
                perror("no free data block");
                return -1;
            }
            indirect_block_offsets[i] = data_block_offset;

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
            return size;
        }

        offset_written += BLOCK_SIZE;

    }
    printf("Too much data written\n");
    return 0;
}

int writeDiskByInode(const char* filepath, int inode_num, const char* buffer, size_t size, off_t wanted_offset) {
    if (size == 0) {
        return 0;
    }

    printf("_____writeDiskByInode\n");

    // Open the file
    int fd = open(filepath, O_RDWR);
    if (fd == -1) {
        perror("error opening file");
        return -1;
    }

    // Fetch the inode
    struct wfs_inode inode = fetch_inode(fd, inode_num);

    // Initialize write tracking variables
    size_t bytes_written = 0;
    size_t offset_written = 0;

    // Direct blocks
    for (int i = 0; i < D_BLOCK; i++) {

        if (inode.blocks[i] == 0) {
            close(fd);
            off_t inodeSize = getInodeSize(inode_num);
            fd = open(filepath, O_RDWR);
            if(inodeSize >= MAX_DIRECT_DATA){
                printf("moving to indirect blocks\n");
                break;
            }
            // Allocate new data block
            off_t data_block_offset = allocate_datablock(fd);
            if (data_block_offset == -1) {
                perror("no free data block");
                close(fd);
                return -1;
            }

            inode.blocks[i] = data_block_offset;
            writeExistingInodeToDisk(fd, inode);
        }

        off_t offset = inode.blocks[i];
        if (lseek(fd, offset, SEEK_SET) == -1) {
            perror("error in lseek data block");
            close(fd);
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
                close(fd);
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
            perror("error in write data block");
            close(fd);
            return -1;
        }

        bytes_written += bytes_to_write;
        if (bytes_written == size) {
            close(fd);
            return size;
        }

        offset_written += BLOCK_SIZE;
    }

    // Indirect blocks
    if (inode.blocks[D_BLOCK] == 0) {
        off_t data_block_offset = allocate_datablock(fd);
        if (data_block_offset == -1) {
            perror("no free data block");
            close(fd);
            return -1;
        }
        inode.blocks[D_BLOCK] = data_block_offset;
        writeExistingInodeToDisk(fd, inode);
    }

    off_t indirect_block_offsets = 0;

    if (lseek(fd, inode.blocks[D_BLOCK], SEEK_SET) == -1) {
        perror("error in lseek indirect block");
        close(fd);
        return -1;
    }

    for (int i = 0; i < MAXIMUM_OFFSES; i++) {
        lseek(fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET);
        read(fd, &indirect_block_offsets, sizeof(off_t));

        if (indirect_block_offsets == 0) {
            off_t data_block_offset = allocate_datablock(fd);
            if (data_block_offset == -1) {
                perror("no free data block");
                close(fd);
                return -1;
            }
            indirect_block_offsets = data_block_offset;

            if (lseek(fd, inode.blocks[D_BLOCK] + (i * sizeof(off_t)), SEEK_SET) == -1) {
                perror("error in lseek indirect block");
                close(fd);
                return -1;
            }

            if (write(fd, &data_block_offset, sizeof(off_t)) != sizeof(off_t)) {
                perror("error in write indirect block");
                close(fd);
                return -1;
            }
        }

        if (lseek(fd, indirect_block_offsets, SEEK_SET) == -1) {
            perror("error in lseek indirect block");
            close(fd);
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
                close(fd);
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
            perror("error in write data block");
            close(fd);
            return -1;
        }

        bytes_written += bytes_to_write;
        if (bytes_written == size) {
            close(fd);
            return size;
        }

        offset_written += BLOCK_SIZE;
    }

    printf("Too much data written\n");

    // Close the file
    close(fd);
    return 0;
}

int writeFileByInode(int inode_num, char* buffer, size_t size, off_t wanted_offset) {

    int number_of_disks = superblock.disk_amount;

    //RAID 0
    if(superblock.raid_mode == 0){
        int diskNumber = 0;
        int itteration = 0;

        size_t bytes_written = 0;
        size_t offset_written = 0;

        while (bytes_written < size ||  offset_written < wanted_offset) {
            size_t bytes = 0;
            size_t offset = 0;

            if(offset_written < wanted_offset){
                offset = Min(wanted_offset - offset_written,BLOCK_SIZE);
            }

            if(bytes_written < size){
                bytes = Min(size - bytes_written,BLOCK_SIZE - offset);
                // bytes = Max(bytes,0);
            }


            printf("diskNumber: %d\n", diskNumber);
            printf("bytes: %ld\n", bytes);
            printf("offset: %ld\n", offset);

            if (offset != 0 || bytes != 0) {
                if(writeDiskByInode(Disks[diskNumber],inode_num, buffer + bytes_written, bytes, offset + (BLOCK_SIZE * itteration)) == -1){
                    return bytes;
                }
            }
            bytes_written += bytes;
            offset_written += offset;

            diskNumber++;
            if(diskNumber == number_of_disks){
                diskNumber = 0;
                itteration++;
            }
        }
    }

    //RAID 1
    if(superblock.raid_mode == 1 || superblock.raid_mode == 2){
        for(int i = 0; i < number_of_disks; i++){
            int fd = open(Disks[i], O_RDWR);
            if(writeDiskByInode1(fd, inode_num, buffer, size, wanted_offset) == -1){
                close(fd);
                return -1;
            }
            close(fd);
            return writeDiskByInode(Disks[i],inode_num, buffer, size, wanted_offset);
        }
    }
}

int make_directory(const char* path, mode_t mode) {

    // check if directory already exists
    if(get_inodeNum_by_path(path, false) != -1) {
        fprintf(stderr, "error: %s already exists\n", path);
        return -EEXIST;
    }

    // get inode number of parent directory
    int parent_inode_num = get_inodeNum_by_path(path, true);

    //read parent
    off_t parent_size = getInodeSize(parent_inode_num);
    char buffer[parent_size];
    readFileByInode(parent_inode_num, buffer, parent_size, 0);

    // create new inode
    struct wfs_inode new_inode;
    new_inode.mode = mode;
    new_inode.uid = getuid();
    new_inode.gid = getgid();
    new_inode.size = 0;
    new_inode.nlinks = 2;
    new_inode.atim = time(NULL);
    new_inode.mtim = time(NULL);
    new_inode.ctim = time(NULL);
    memset(new_inode.blocks, 0, sizeof(new_inode.blocks));
    int new_inode_num = writeNewInodeToDisk(new_inode);
    if(new_inode_num == -1 || new_inode_num == -ENOSPC) {
        return new_inode_num;
    }
    //get dentry name
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");

    int current_inode_number = 0;

    while (nextToken != NULL) {
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    // add new wf_dentry
    struct wfs_dentry new_dentry;
    strcpy(new_dentry.name, token);
    new_dentry.num = new_inode_num;
    printf("new_dentry.name: %s\n", new_dentry.name);
    printf("new_dentry.num: %d\n", new_dentry.num);

   

    off_t offset = 0;
    // write to parent
    while (offset < parent_size) {
        struct wfs_dentry entry;
        memcpy(&entry, buffer + offset, sizeof(entry));
        if (entry.num == 0) {
            break;
        }
        offset += sizeof(entry);
    }
    printf(">>offset: %ld\n", offset);
    writeFileByInode(parent_inode_num, (char*)&new_dentry, sizeof(new_dentry), offset);
}


static int wfs_getattr(const char* path, struct stat* stbuf) {

    printf("------%s------", path);
   if (path == NULL) {
        // return 0;
       return -ENOENT;
   }
   memset(stbuf, 0, sizeof(struct stat));
   int inode;
   int fd;


   if (strcmp(path, "/") == 0) {
        fd = open(Disks[0], O_RDWR);
        if(fd == -1){
            printf("error in open");
            return -1;
        }

       printf("CAUUUGHHHTTTT");
    //    print_Directory(fd, 1);
    //    print_inode(fetch_inode(fd, 1));
    //    print bitmaps
       print_data_block_bitmap(fd);
       print_inode_bitmap(fd);
       close(fd);


       fflush(stdout);
       inode = 0;
   }
   else {
       inode = get_inodeNum_by_path(path, false);
   }
   if (inode == -1) {
    // return 0;
       return -ENOENT;
   }

    fd = open(Disks[0], O_RDWR);
    struct wfs_inode wfs_inode = fetch_inode(fd, inode);
    close(fd);


   stbuf->st_uid = wfs_inode.uid;
   stbuf->st_gid = wfs_inode.gid;
   stbuf->st_size = wfs_inode.size;
   stbuf->st_atime = wfs_inode.atim;
   stbuf->st_mtime = wfs_inode.mtim;
   stbuf->st_ctime = wfs_inode.ctim;
   stbuf->st_mode = wfs_inode.mode;
   fflush(stdout);


   printf("getattr called for path: %s\n", path);
   return 0;
}

// static int wfs_read(const char* path, char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
static int wfs_read(const char* path, char* buf, size_t size, off_t offset) {

    int inode_num = get_inodeNum_by_path(path, false);
    if (inode_num == -1) {
        return -ENOENT;
    }

    readFileByInode(inode_num, buf, size, offset);
    return size;
}

static int wfs_mknod(const char* path, mode_t mode, dev_t rdev) {
    int ret = make_directory(path, mode|S_IFREG);
    return ret == -ENOSPC? ret : 0;
}

static int wfs_mkdir(const char* path, mode_t mode) {
    int ret = make_directory(path, mode|S_IFDIR);
    return ret == -ENOSPC? ret : 0;
}

int removeItem(const char* path){
    //remove directory from parent
    int parent_inode_num = get_inodeNum_by_path(path, true);
    int child_inode_num = get_inodeNum_by_path(path, false);

    //get dentry name
    char* token;
    char* nextToken;
    char* path_copy = strdup(path);
    token = strtok(path_copy, "/");
    nextToken = strtok(NULL, "/");

    while (nextToken != NULL) {
        token = nextToken;
        nextToken = strtok(NULL, "/");
    }

    for(int i = 0; i < superblock.disk_amount; i++){

        int fd = open(Disks[i], O_RDWR);

        //get the inode bitmap
        size_t inode_bitmap_size = (superblock.num_inodes + 7) / 8;
        char* inode_bitmap = (char*)malloc(inode_bitmap_size);


        get_inode_bitmap(fd, inode_bitmap);

        inode_bitmap[child_inode_num / 8] &= ~(1 << (child_inode_num % 8));

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

        //remove all data blocks
        size_t data_block_bitmap_size = (superblock.num_data_blocks + 7) / 8;
        char* data_block_bitmap = (char*)malloc(data_block_bitmap_size);
        get_data_block_bitmap(fd, data_block_bitmap);

        struct wfs_inode child_inode = fetch_inode(fd, child_inode_num);

        if(child_inode.blocks[D_BLOCK] != 0){
            char buffer[BLOCK_SIZE];

            lseek(fd, child_inode.blocks[D_BLOCK], SEEK_SET);
            read(fd, buffer, BLOCK_SIZE);

            for(int i = 0; i < MAXIMUM_OFFSES; i++){
                if(((off_t*)buffer)[i] == 0){
                    continue;
                }

                int real_block = (((off_t*)buffer)[i] - superblock.d_blocks_ptr) / BLOCK_SIZE;
                data_block_bitmap[real_block / 8] &= ~(1 << (real_block % 8));
            }
            
        }

        for (int i = 0; i <= D_BLOCK; i++) {

            // printf("child_inode.blocks[%d]: %d\n", i, x);
            if (child_inode.blocks[i] == 0) {
                continue;
            }

            int real_block = (child_inode.blocks[i] - superblock.d_blocks_ptr) / BLOCK_SIZE;
            data_block_bitmap[real_block / 8] &= ~(1 << (real_block % 8));
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
        close(fd);
    }

    for (int i = 0; i < superblock.disk_amount; i++) {
        int fd = open(Disks[i], O_RDWR);
        //get the parent denrty
        off_t size = getInodeSize(parent_inode_num);
        char buffer[size];
        readFileByInode(parent_inode_num, buffer, size, 0);
        //find the dentry in parent
        off_t offset = 0;
        bool found = false;
        struct wfs_dentry entry;


        while (offset < size) {
            memcpy(&entry, buffer + offset, sizeof(entry));
            if (strcmp(entry.name, token) == 0 || entry.num != 0) {
                found = true;
                break;
            }
            offset += sizeof(entry);
        }
        if (!found) {
            printf("File not found %s\n", path);
            break;
        }

        //remove the dentry
        struct wfs_dentry new_dentry;
        strcpy(new_dentry.name, "");
        new_dentry.num = 0;
        writeFileByInode(parent_inode_num, (char*)&new_dentry, sizeof(new_dentry), offset);
        close(fd);
    }

    return 0;

}

//delete file
static int wfs_unlink(const char* path) {
    removeItem(path);
}

// static int wfs_write(const char* path, const char* buf, size_t size, off_t offset, struct fuse_file_info* fi) {
static int wfs_write(const char* path, const char* buf, size_t size, off_t offset) {

    int inode_num = get_inodeNum_by_path(path, true);
    //make the file if it does not exist
    if (inode_num == -1) {
        return -ENOENT;
    }
    wfs_mknod(path, 0777, 0);
    inode_num = get_inodeNum_by_path(path, false);


    writeFileByInode(inode_num, buf, size, offset);
    return size;
}

static int wfs_rmdir(const char* path) {
    return removeItem(path);
}

static int wfs_readdir(const char* path, void* buf, off_t __offsetof) {
   int inode_number = get_inodeNum_by_path(path,false);
   off_t inode_size = getInodeSize(inode_number);
   char buffer[inode_size];
    printf("reached readdir\n");

   readFileByInode(inode_number, buffer, inode_size, 0);
 printf("reached readdir\n");
    struct wfs_dentry entry;
    off_t offset = 0;
    printf("offset: %ld\n", offset);
        printf("inode_size: %ld\n", inode_size);
    while (offset < inode_size) {
        printf("offset: %ld\n", offset);
        printf("inode_size: %ld\n", inode_size);
        memcpy(&entry, buffer + offset, sizeof(entry));
        if (entry.num != 0) {
            //add name to buffer
            printf("entry.name: %s\n", entry.name);
        }
        offset += sizeof(entry);
        fflush(stdout);
    }
   return 0;
}



// TODO Callback function for reading a directory (listing files)

int main(int argc, char
  const * argv[]) {
  strcpy(Disks[0], "test-disk1.img");
  strcpy(Disks[1], "test-disk2.img");
//   strcpy(Disks[2], "test-disk3.img");
  strcpy(Disks[2], "\0");

  // printf("Disks[0]: %s\n", Disks[0]);

  int fd = open(Disks[0], O_RDWR);
  superblock = get_superblock(fd);
  // struct wfs_inode wfs_inode = fetch_inode(fd, 0);
  close(fd);

//   fd = open(Disks[0], O_RDWR);
//     print_inode_bitmap(fd);
//     print_data_block_bitmap(fd);
//     close(fd);

 fflush(stdout);

// wfs_mknod("/boo", 0777, 0);
// for(int i =0; i< 32; i ++){
//     char fileName[4] = {'/','a' + i, '.', 't'};
//     int inode = wfs_mknod(fileName, 0777, 0);
//     printf("inode: %d\n", inode);
    
//     fd = open(Disks[0], O_RDWR);
//     print_inode_bitmap(fd);
//     close(fd);
//   }

int file_size = 30;
  char toWrite[file_size];
  for (int i = 0; i < file_size; i++) {
    if (i % BLOCK_SIZE == 0) {
      toWrite[i] = '_';
      printf("%c", 'a' + (i / BLOCK_SIZE));
    } else {
      toWrite[i] = 'a' + (i / BLOCK_SIZE);
    }
  }

wfs_write("/file1", toWrite, file_size, 0);
// wfs_write("/file2", toWrite, file_size, 0);
// wfs_write("/file3", toWrite, file_size, 0);

char buffer[file_size];
wfs_read("/file1", buffer, file_size, 0);
printf("buffer: %s\n", buffer);

// wfs_readdir("/", NULL, 0);


// file_size = 1536;
//   toWrite[file_size];
//   for (int i = 0; i < file_size; i++) {
//     if (i % BLOCK_SIZE == 0) {
//       toWrite[i] = '_';
//       printf("%c", 'a' + (i / BLOCK_SIZE));
//     } else {
//       toWrite[i] = 'a' + (i / BLOCK_SIZE);
//     }
//   }

// wfs_write("/file1", toWrite, file_size, 0);


//   wfs_write("/test.txt", toWrite, file_size, 0);

//   fd = open(Disks[0], O_RDWR);
//   print_data_block_bitmap(fd);
//   print_inode_bitmap(fd);
//   close(fd);

//   fd = open(Disks[1], O_RDWR);
//   print_data_block_bitmap(fd);
//   print_inode_bitmap(fd);
//   close(fd);


}