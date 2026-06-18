/****************************************
Author：LiujiaHuan
email:cheayuki13@gmail.com
Date: 2024/06/19
PerfInsight research prototype.
************************************** */
#ifndef __PERFINSIGHT__
#define __PERFINSIGHT__

#include <stdio.h>
#include <stdlib.h>
#include <yaml.h>
#include <string.h>

#define MAX_PROGRAMS 13
#define NUM_SUPPORT_PROG 13
#define MAX_ARGS 16

typedef struct {
    char* program;
    char* args[MAX_ARGS];
    int args_count;
} ProgramConfig;

typedef struct {
    void *(*func)();
    const char *name;
} ThreadInfo;

extern int psi_handle_flag; //判断是否启用psi触发
extern int trigger_flag;    //基于启用了psi触发，让子程序判断是否执行

int load_programs_from_yaml(const char *filename, ProgramConfig programs[], int max_programs);


extern int thread_launched;
extern ThreadInfo supported_programs[];


#endif

