
#include "pipe.h"
#include "memory.h"
#include "fs.h"
#include "file.h"
#include "ioqueue.h"
#include "thread.h"

/* 判断文件描述符local_fd是否是管道 */
bool is_pipe(uint32_t local_fd) {
   uint32_t global_fd = fd_local2global(local_fd); 
   return file_table[global_fd].fd_flag == PIPE_FLAG;
}

/* 创建管道,成功返回0,失败返回-1 */
int32_t sys_pipe(int32_t pipefd[2]) {
   int32_t global_fd = get_free_slot_in_global();

   /* 申请一页内核内存做环形缓冲区 */
   file_table[global_fd].fd_inode = get_kernel_pages(1); 

   /* 初始化环形缓冲区 */
   ioqueue_init((struct ioqueue*)file_table[global_fd].fd_inode);
   if (file_table[global_fd].fd_inode == NULL) {
      return -1;
   }
  
   /* 将fd_flag复用为管道标志 */
   file_table[global_fd].fd_flag = PIPE_FLAG;

   /* 将fd_pos复用为管道打开数 */
   file_table[global_fd].fd_pos = 2;
   pipefd[0] = pcb_fd_install(global_fd);
   pipefd[1] = pcb_fd_install(global_fd);
   return 0;
}

/* 从管道中读数据 */
uint32_t pipe_read(int32_t fd, void* buf, uint32_t count) {
   char* buffer = buf;
   uint32_t bytes_read = 0;
   uint32_t global_fd = fd_local2global(fd);

   /* 获取管道的环形缓冲区 */
   struct ioqueue* ioq = (struct ioqueue*)file_table[global_fd].fd_inode;

   /* 选择较小的数据读取量,避免阻塞 */
   uint32_t ioq_len = ioq_length(ioq);
   uint32_t size = ioq_len > count ? count : ioq_len;
   while (bytes_read < size) {
      *buffer = ioq_getchar(ioq);
      bytes_read++;
      buffer++;
   }
   return bytes_read;
}

/* 分析字符串cmd_str中以token为分隔符的单词,将各单词的指针存入argv数组 */
static int32_t cmd_parse(char* cmd_str, char** argv, char token) {
   assert(cmd_str != NULL);
   int32_t arg_idx = 0;
   while(arg_idx < MAX_ARG_NR) {
      argv[arg_idx] = NULL;
      arg_idx++;
   }
   char* next = cmd_str;
   int32_t argc = 0;
   /* 外层循环处理整个命令行 */
   while(*next) {
      /* 去除命令字或参数之间的空格 */
      while(*next == token) {
	 next++;
      }
      /* 处理最后一个参数后接空格的情况,如"ls dir2 " */
      if (*next == 0) {
	 break; 
      }
      argv[argc] = next;

     /* 内层循环处理命令行中的每个命令字及参数 */
      while (*next && *next != token) {	  // 在字符串结束前找单词分隔符
	 next++;
      }

      /* 如果未结束(是token字符),使tocken变成0 */
      if (*next) {
	 *next++ = 0;	// 将token字符替换为字符串结束符0,做为一个单词的结束,并将字符指针next指向下一个字符
      }
   
      /* 避免argv数组访问越界,参数过多则返回0 */
      if (argc > MAX_ARG_NR) {
	 return -1;
      }
      argc++;
   }
   return argc;
}
