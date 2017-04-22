#include "dir.h"
#include "stdint.h"
#include "inode.h"
#include "file.h"
#include "fs.h"
#include "stdio-kernel.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "string.h"
#include "interrupt.h"
#include "super_block.h"

struct dir root_dir;             // 根目录

/* 打开根目录 */
void open_root_dir(struct partition* part) {
   root_dir.inode = inode_open(part, part->sb->root_inode_no);
   root_dir.dir_pos = 0;
}

/* 在分区part上打开i结点为inode_no的目录并返回目录指针 */
struct dir* dir_open(struct partition* part, uint32_t inode_no) {
   struct dir* pdir = (struct dir*)sys_malloc(sizeof(struct dir));
   pdir->inode = inode_open(part, inode_no);
   pdir->dir_pos = 0;
   return pdir;
}

/* 在part分区内的pdir目录内寻找名为name的文件或目录,
 * 找到后返回true并将其目录项存入dir_e,否则返回false */
bool search_dir_entry(struct partition* part, struct dir* pdir, \
		     const char* name, struct dir_entry* dir_e) {
   uint32_t block_cnt = 140;	 // 12个直接块+128个一级间接块=140块

   /* 12个直接块大小+128个间接块,共560字节 */
   uint32_t* all_blocks = (uint32_t*)sys_malloc(48 + 512);
   if (all_blocks == NULL) {
      printk("search_dir_entry: sys_malloc for all_blocks failed");
      return false;
   }

   uint32_t block_idx = 0;
   while (block_idx < 12) {
      all_blocks[block_idx] = pdir->inode->i_sectors[block_idx];
      block_idx++;
   }
   block_idx = 0;

   if (pdir->inode->i_sectors[12] != 0) {	// 若含有一级间接块表
      ide_read(part->my_disk, pdir->inode->i_sectors[12], all_blocks + 12, 1);
   }
