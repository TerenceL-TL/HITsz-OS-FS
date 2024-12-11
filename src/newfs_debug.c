#include "../include/newfs.h"

extern struct newfs_super      super; 
extern struct custom_options newfs_options;

void newfs_dump_imap() {
    int byte_cursor = 0;
    int bit_cursor = 0;

    printf("-----------------------------Inode Map-----------------------------------\n");
    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.ino_map_blks); 
         byte_cursor+=4)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.ino_map[byte_cursor] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");

        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.ino_map[byte_cursor + 1] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.ino_map[byte_cursor + 2] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.ino_map[byte_cursor + 3] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\n");
        break;
    }
}

void newfs_dump_dmap() {
    int byte_cursor = 0;
    int bit_cursor = 0;

    printf("-----------------------------Data Map-----------------------------------\n");

    for (byte_cursor = 0; byte_cursor < NFS_BLKS_SZ(super.data_map_blks); 
         byte_cursor+=4)
    {
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.data_map[byte_cursor] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");

        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.data_map[byte_cursor + 1] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.data_map[byte_cursor + 2] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\t");
        
        for (bit_cursor = 0; bit_cursor < UINT8_BITS; bit_cursor++) {
            printf("%d ", (super.data_map[byte_cursor + 3] & (0x1 << bit_cursor)) >> bit_cursor);   
        }
        printf("\n");
        break;
    }
}