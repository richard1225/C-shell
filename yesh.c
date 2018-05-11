#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h> 
#include <sys/types.h>
#include <sys/wait.h>

static char* args[512];
#define READ  0
#define WRITE 1
#define MAXBUFSIZE 1024
#define EST (8)

static int command(int input, int first, int last)
{
	/* input 是上一个进程的输入管道描述符， 本次进程将在input中读取上个进程的输入
	   first，last 是标志符号，代表本次进程的执行位置

	   1. first == 1 && last ==0 && input == 0 说明本次是第一次执行程序，输入管道是0代表没有输入
	   2. first == 0 && last == 0 && input != 0 说明本次不是第一次执行程序，也不是最后一次执行程序，input！=0代表有上一个进程的输出，输入进来
	   3. first == 0 && last == 1 && input != 0 说明本组程序中的最后一个程序
	*/
	pid_t pid;
	int pipettes[2];
	
	pipe(pipettes);	
	pid = fork();
	
	// 子进程
	if (pid == 0) {
		if (first == 1 && last == 0 && input == 0) {
			// 第一条指令，绑定管道的写端到标准输出，标准输出的内容都会写入写管道
			dup2( pipettes[WRITE], STDOUT_FILENO );
		} else if (first == 0 && last == 0 && input != 0) {
			// 中间指令，绑定管道的读端到标准输入，以及绑定管道的写端到标准输出，标准输出的内容都会写进写管道
			dup2(input, STDIN_FILENO);
			dup2(pipettes[WRITE], STDOUT_FILENO);
		} else {
			// 最后一条指令
			dup2( input, STDIN_FILENO );
		}
		if (execvp( args[0], args) == -1)
			// 如果指令执行失败，直接退出
			_exit(EXIT_FAILURE); 
	}

	if (input != 0) 
		close(input);
 
	// 关闭写端
	close(pipettes[WRITE]);

	// 最后一条指令执行完之后，不需要再读取，所以关闭读端
	if (last == 1)
		close(pipettes[READ]);
	
	// 返回本次进程的管道的读端，给下一个进程读取本次处理的信息（如果后面还有进程）
	return pipettes[READ];
}
 
static void wait_all(int n)
{
	/* 父进程等待所有子进程结束并回收子进程资源，防止出现僵尸进程*/
	int i;
	for (i = 0; i < n; ++i){ 
		int status; // 用于获取子进程的执行状态
		int pid = 0;
		pid = wait(&status); 
		int not_find_id = 256;
		if(status == not_find_id)
		  // 如果子进程的结束状态是256，说明找不到这个指令
		  printf("yesh: command not found\n");
	}
}

static void print_title() {
	/* 输出用户名，路径，时间等信息，附带了颜色控制符*/
	char * buf1 =  (char*)malloc(MAXBUFSIZE+1);
	getcwd(buf1, MAXBUFSIZE);
	time_t raw_time;
	struct tm *ptr_ts;
	time ( &raw_time );
	ptr_ts = gmtime ( &raw_time );

	printf("\n\033[0;34m#\033[0m \033[0;32m%s\033[0m in ", getlogin());
	printf("\033[1;33m%s\033[0m",buf1);
	printf (" [%2d:%02d:%02d]\n",
	ptr_ts->tm_hour+EST, ptr_ts->tm_min, ptr_ts->tm_sec);
	free(buf1);
}

static int run(char* cmd, int input, int first, int last);
static char line[1024];
static int n = 0; // 用于记录产生的子进程个数
 
int main()
{
	printf("Yesh -> 1.0\nEnther 'exit' to exit.");
	int counter = 0;
	while (1) {
		counter += 1;
		// 每次都输出当前路径和时间
		print_title();
		printf("→ [%d]: ",counter);
 
		/* Read a command line */
		if (!fgets(line, 1024, stdin)) 
			return 0;
		

		int input = 0;
		int first = 1;
 
		char* cmd = line;
		char* next = strchr(cmd, '|'); // 指针指向第一个出现 '|' 的地址
		while (next != NULL) {
			// 截断line，只留下当前指令
			*next = '\0';
			input = run(cmd, input, first, 0);
			// 如果是cd命令，则无需执行接下来的子进程
			if(input == -99999)
				break;
 
			cmd = next + 1;
			next = strchr(cmd, '|'); // 寻找下一个'|'的位置
			first = 0;
		}
		input = run(cmd, input, first, 1);
		wait_all(n);
		n = 0;
	}
	return 0;
}
 
static void split(char* cmd);
 
static int run(char* cmd, int input, int first, int last)
{
	split(cmd);
	if (args[0] != NULL) {
		if (strcmp(args[0], "exit") == 0) 
			exit(0);

		if (strcmp(args[0], "cd") == 0){ 	
			/* 检查是否是cd命令，如果是cd命令，主进程就会调用chdir
			   来变更路径，并返回-999999后就会直接break
			*/
			chdir(args[1]);
			return -99999;
		}
		n += 1;	// 如果不是退出和cd命令，则把子进程数+1，并调用command来执行子进程。
		return command(input, first, last);
	}
	return 0;
}
 
static char* skipwhite(char* s)
{
	// 从s的地址开始，去除s之后的所有空格，直到遇到第一个非空格位置，返回此时s的地址
	while (isspace(*s)) ++s;
	return s;
}
 
static void split(char* cmd)
{
	cmd = skipwhite(cmd);	// 去除所有空格
	char* next = strchr(cmd, ' '); // 找到第一个空格所出现的位置
	int i = 0;
 
	while(next != NULL) {
		// 用\0来截断字符串，只读取当前的指令
		next[0] = '\0';
		// 把指令的每个参数加入args数组，作为环境变量
		args[i] = cmd;
		++i;
		cmd = skipwhite(next + 1);
		next = strchr(cmd, ' '); // 找到下一个空格的位置并指向它
	}
	
	// 如果当前指令是最后一条指令
	if (cmd[0] != '\0') {
		args[i] = cmd;
		next = strchr(cmd, '\n');
		next[0] = '\0';
		++i; 
	}
 
	args[i] = NULL;
}
