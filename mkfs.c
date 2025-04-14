#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <ctype.h>
#include <time.h>
#include "wfs.h"

#define MAX_DISK_IMAGE_AMOUNT 256


int RaidMode = -1;
char* DiskImageFile[MAX_DISK_IMAGE_AMOUNT];
int InodesAmount = -1;
int BlocksAmount = -1;

int getNextDivisbleNumber(int num, int x) {
    return (num + x - 1) / x * x;
}

int divideRoundingUp(int x, int y){
    return (x + y - 1) / y;
}

int initialize_disk(char* disk_image, int diskAmount){

    // Open all disk files and validate their sizes
    int fd = open(disk_image, O_RDWR , 0666);
    if (fd == -1){
        perror("error in open\n");
        return -1;
    }
    // if (ftruncate(fd, size) == -1){
    //     perror("error in ftruncate");
    //     return -1;
    // }

    // Create superblock
    struct wfs_sb superblock;
    superblock.num_inodes = InodesAmount;
    superblock.num_data_blocks = BlocksAmount;
    superblock.i_bitmap_ptr = sizeof(struct wfs_sb);
    superblock.d_bitmap_ptr = superblock.i_bitmap_ptr + divideRoundingUp(InodesAmount,8);
    superblock.i_blocks_ptr = getNextDivisbleNumber(superblock.d_bitmap_ptr + divideRoundingUp(BlocksAmount,8),BLOCK_SIZE);
    superblock.d_blocks_ptr = superblock.i_blocks_ptr + (InodesAmount * BLOCK_SIZE);
    superblock.raid_mode = RaidMode;

    superblock.disk_amount = diskAmount;

    // printf("\n\n%jd\n",superblock.i_bitmap_ptr);
    // printf("%jd\n",superblock.d_bitmap_ptr);
    // printf("%jd\n",superblock.i_blocks_ptr);
    // printf("%jd\n",superblock.d_blocks_ptr);
    // printf("%d\n",superblock.raid_mode);

    if (write(fd, &superblock, sizeof(superblock)) == -1){
        perror("error in write\n");
        return -1;
    }

    // write indode bitmap
    int inode_bitmap[InodesAmount / 8];
    memset(inode_bitmap, 0, sizeof(inode_bitmap));
    inode_bitmap[0] |= 1;
    // inode_bitmap[0] |= (1 << 7);
    if (write(fd, inode_bitmap, sizeof(inode_bitmap)) == -1){
        perror("error in write\n");
        return -1;
    }

    // write datablock bitmap
    int data_block_bitmap[BlocksAmount / 8];
    memset(data_block_bitmap, 0, sizeof(data_block_bitmap));
    if (write(fd, data_block_bitmap, sizeof(data_block_bitmap)) == -1){
        perror("error in write\n");
        return -1;
    }

    //int     num;      /* Inode number */
    // mode_t  mode;     /* File type and mode */
    // uid_t   uid;      /* User ID of owner */
    // gid_t   gid;      /* Group ID of owner */
    // off_t   size;     /* Total size, in bytes */
    // int     nlinks;   /* Number of links */

    // time_t atim;      /* Time of last access */
    // time_t mtim;      /* Time of last modification */
    // time_t ctim;      /* Time of last status change */

    // off_t blocks[N_BLOCKS];
    struct wfs_inode root_inode;
    root_inode.num = 0;
    root_inode.mode = 16877;
    root_inode.uid = getuid();
    root_inode.gid = getgid();
    root_inode.size = 0;
    root_inode.nlinks = 1;

    root_inode.atim = time(NULL);
    root_inode.mtim = time(NULL);
    root_inode.ctim = time(NULL);

    memset(root_inode.blocks, 0, sizeof(root_inode.blocks));
    // printf("%jd\n",superblock.i_blocks_ptr);
    if (lseek(fd, superblock.i_blocks_ptr, SEEK_SET) == -1) {
        perror("error in lseek\n");
        return -1;
    }

    if (write(fd, &root_inode, sizeof(root_inode)) == -1) {
        perror("error in write\n");
        return -1;
    }

    off_t file_size = lseek(fd, 0, SEEK_END);
    off_t needed_size = (superblock.d_blocks_ptr + (BlocksAmount * BLOCK_SIZE));
    // printf("\n\n%jd\n\n", (file_size));
    // printf("\n\n%jd\n\n", (needed_size));
    if(needed_size > file_size){
        printf("too small file\n");
        exit(255);
    }
    return 0;
}


int main (int argc, char **argv){
    //Break Command and get Args
    char param = '\0';
    int diskImageAmount = 0;
    for(int i = 1; i < argc; i++){
        if (strcmp(argv[i], "-r") == 0) param = 'r';
        else if (strcmp(argv[i], "-d") == 0) param = 'd';
        else if (strcmp(argv[i], "-i") == 0) param = 'i';
        else if (strcmp(argv[i], "-b") == 0) param = 'b';
        else {
            switch (param){
            case 'r':
                if(strcmp(argv[i], "1v") == 0){
                    RaidMode = 2;
                }else{
                    RaidMode = atoi(argv[i]);
                    if(RaidMode != 1 && RaidMode != 0){
                        printf("Bad Raid Mode\n");
                        exit(1);
                    }
                }
                break;
            case 'd':
                DiskImageFile[diskImageAmount++] = argv[i];
                break;
            case 'i':
                InodesAmount = atoi(argv[i]);
                break;
            case 'b':
                BlocksAmount = atoi(argv[i]);
                break;
            default:
                exit(1);
                break;
            param = '\0';
            }
        }
    }
    DiskImageFile[diskImageAmount] = NULL;

    if (RaidMode == -1 || InodesAmount == -1 || BlocksAmount == -1 || diskImageAmount == 0){
        printf("Missing parameters\n");
        exit(1);
    }

    BlocksAmount = getNextDivisbleNumber(BlocksAmount,32);
    InodesAmount = getNextDivisbleNumber(InodesAmount,32);

    for (int i = 0; i < diskImageAmount; i++){
        // int fd = open(DiskImageFile[i], O_RDWR , 0666);
        // off_t file_size = lseek(fd, 0, SEEK_END);
        // printf("\n\n\n%jd\n\n\n",file_size);
        if (initialize_disk(DiskImageFile[i],diskImageAmount) == -1){
            exit(1);
        }
    }


    // printf("BADDDD\n");
    return 0;
}