#include "fs.h"
#include "super_block.h"
#include "inode.h"
#include "dir.h"
#include "stdint.h"
#include "stdio-kernel.h"
#include "list.h"
#include "string.h"
#include "ide.h"
#include "global.h"
#include "debug.h"
#include "memory.h"
#include "file.h"
#include "console.h"
#include "keyboard.h"
#include "ioqueue.h"
#include "pipe.h"

struct partition* cur_part;	 // 默认情况下操作的是哪个分区

/* 在分区链表中找到名为part_name的分区,并将其指针赋值给cur_part */
static bool mount_partition(struct list_elem* pelem, int arg) {
   char* part_name = (char*)arg;
   struct partition* part = elem2entry(struct partition, part_tag, pelem);
   if (!strcmp(part->name, part_name)) {
      cur_part = part;
      struct disk* hd = cur_part->my_disk;

      /* sb_buf用来存储从硬盘上读入的超级块 */
      struct super_block* sb_buf = (struct super_block*)sys_malloc(SECTOR_SIZE);

      /* 在内存中创建分区cur_part的超级块 */
      cur_part->sb = (struct super_block*)sys_malloc(sizeof(struct super_block));
      if (cur_part->sb == NULL) {
	 PANIC("alloc memory failed!");
      }

      /* 读入超级块 */
      memset(sb_buf, 0, SECTOR_SIZE);
      ide_read(hd, cur_part->start_lba + 1, sb_buf, 1);   

      /* 把sb_buf中超级块的信息复制到分区的超级块sb中。*/
      memcpy(cur_part->sb, sb_buf, sizeof(struct super_block)); 

      /**********     将硬盘上的块位图读入到内存    ****************/
      cur_part->block_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->block_bitmap_sects * SECTOR_SIZE);
      if (cur_part->block_bitmap.bits == NULL) {
	 PANIC("alloc memory failed!");
      }
      cur_part->block_bitmap.btmp_bytes_len = sb_buf->block_bitmap_sects * SECTOR_SIZE;
      /* 从硬盘上读入块位图到分区的block_bitmap.bits */
      ide_read(hd, sb_buf->block_bitmap_lba, cur_part->block_bitmap.bits, sb_buf->block_bitmap_sects);   
      /*************************************************************/

      /**********     将硬盘上的inode位图读入到内存    ************/
      cur_part->inode_bitmap.bits = (uint8_t*)sys_malloc(sb_buf->inode_bitmap_sects * SECTOR_SIZE);
      if (cur_part->inode_bitmap.bits == NULL) {
	 PANIC("alloc memory failed!");
      }
      cur_part->inode_bitmap.btmp_bytes_len = sb_buf->inode_bitmap_sects * SECTOR_SIZE;
      /* 从硬盘上读入inode位图到分区的inode_bitmap.bits */
      ide_read(hd, sb_buf->inode_bitmap_lba, cur_part->inode_bitmap.bits, sb_buf->inode_bitmap_sects);   
      /*************************************************************/

      list_init(&cur_part->open_inodes);
      printk("mount %s done!\n", part->name);

   /* 此处返回true是为了迎合主调函数list_traversal的实现,与函数本身功能无关。
      只有返回true时list_traversal才会停止遍历,减少了后面元素无意义的遍历.*/
      return true;
   }
   return false;     // 使list_traversal继续遍历
}

/* 格式化分区,也就是初始化分区的元信息,创建文件系统 */
static void partition_format(struct partition* part) {
/* 为方便实现,一个块大小是一扇区 */
   uint32_t boot_sector_sects = 1;	  
   uint32_t super_block_sects = 1;
   uint32_t inode_bitmap_sects = DIV_ROUND_UP(MAX_FILES_PER_PART, BITS_PER_SECTOR);	   // I结点位图占用的扇区数.最多支持4096个文件
   uint32_t inode_table_sects = DIV_ROUND_UP(((sizeof(struct inode) * MAX_FILES_PER_PART)), SECTOR_SIZE);
   uint32_t used_sects = boot_sector_sects + super_block_sects + inode_bitmap_sects + inode_table_sects;
   uint32_t free_sects = part->sec_cnt - used_sects;  

/************** 简单处理块位图占据的扇区数 ***************/
   uint32_t block_bitmap_sects;
   block_bitmap_sects = DIV_ROUND_UP(free_sects, BITS_PER_SECTOR);
   /* block_bitmap_bit_len是位图中位的长度,也是可用块的数量 */
   uint32_t block_bitmap_bit_len = free_sects - block_bitmap_sects; 
   block_bitmap_sects = DIV_ROUND_UP(block_bitmap_bit_len, BITS_PER_SECTOR); 
/*********************************************************/
   
   /* 超级块初始化 */
   struct super_block sb;
   sb.magic = 0x19590318;
   sb.sec_cnt = part->sec_cnt;
   sb.inode_cnt = MAX_FILES_PER_PART;
   sb.part_lba_base = part->start_lba;

   sb.block_bitmap_lba = sb.part_lba_base + 2;	 // 第0块是引导块,第1块是超级块
   sb.block_bitmap_sects = block_bitmap_sects;

   sb.inode_bitmap_lba = sb.block_bitmap_lba + sb.block_bitmap_sects;
   sb.inode_bitmap_sects = inode_bitmap_sects;

   sb.inode_table_lba = sb.inode_bitmap_lba + sb.inode_bitmap_sects;
   sb.inode_table_sects = inode_table_sects; 

   sb.data_start_lba = sb.inode_table_lba + sb.inode_table_sects;
   sb.root_inode_no = 0;
   sb.dir_entry_size = sizeof(struct dir_entry);

   printk("%s info:\n", part->name);
   printk("   magic:0x%x\n   part_lba_base:0x%x\n   all_sectors:0x%x\n   inode_cnt:0x%x\n   block_bitmap_lba:0x%x\n   block_bitmap_sectors:0x%x\n   inode_bitmap_lba:0x%x\n   inode_bitmap_sectors:0x%x\n   inode_table_lba:0x%x\n   inode_table_sectors:0x%x\n   data_start_lba:0x%x\n", sb.magic, sb.part_lba_base, sb.sec_cnt, sb.inode_cnt, sb.block_bitmap_lba, sb.block_bitmap_sects, sb.inode_bitmap_lba, sb.inode_bitmap_sects, sb.inode_table_lba, sb.inode_table_sects, sb.data_start_lba);

   struct disk* hd = part->my_disk;
