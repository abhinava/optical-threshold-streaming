/**
 * process_mon.c
 *
 * Populates the openconfig-system.yang subtree
 * /oc-sys:system/oc-sys:processes/oc-sys:process with the
 * current set of processes running on the operating system.
 *
 * Abhinava Sadasivarao
 * (c) Infinera Corporation, 2020
 */

#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <signal.h>

#include <confd_lib.h>
#include <confd_cdb.h>

#include <algorithm>
#include <iterator>
#include <iostream>
#include <cstdio>
#include <ctime>
#include <vector>
#include <fstream>
#include <sstream>
#include <inttypes.h>

#include "openconfig-system.h"

#define INTERVAL 10
#define MAX_SAMPLES (86400/interval)

#define PROC_NAME 128
#define PROC_ARGS 1024

#define OK(rval) do {                                                   \
        if ((rval) != CONFD_OK)                                         \
            confd_fatal("get_load: error not CONFD_OK: %d : %s\n",      \
                        confd_errno, confd_lasterr());                  \
    } while (0);

struct pinfo_t {
    uint8_t cpu_utilization;
    uint8_t memory_utilization;
    uint64_t pid;
    uint64_t start_time;
    uint64_t cpu_usage_user;
    uint64_t cpu_usage_system;
    uint64_t memory_usage;
    std::string name;
    std::vector<std::string> args;
};

typedef struct pinfo_t pinfo_t;

static std::string createTempFileName(void)
{
    char buffer[L_tmpnam];
    tmpnam(buffer);
    std::string tmpFilename = std::string(buffer);

    return tmpFilename;
}

static bool deleteTempFile(std::ifstream& file, std::string fileName)
{
    file.close();
    remove(fileName.c_str());
    return true;
}

static void getUsageTime(std::string pid, uint64_t& userTime, uint64_t& kernelTime)
{
    std::string statFilename = createTempFileName();
    std::string cmd = "cat /proc/" + pid + "/stat" + " > " + statFilename;
    system(cmd.c_str());

    std::ifstream inFile(statFilename);        
    std::string line;
    std::getline(inFile, line);
    std::istringstream iss(line);

    /**
     * Reference: https://linux.die.net/man/5/proc
     *
     * Skip the first 13 whitespace separated tokens
     */
    int count = 13;
    std::string token;

    while (count-- != 0) {
        iss >> token;
    }

    iss >> token;
    userTime = (uint64_t) std::atoi(token.c_str());
    iss >> token;
    kernelTime = (uint64_t) std::atoi(token.c_str());

    deleteTempFile(inFile, statFilename);
}

static std::vector<pinfo_t> get_system_processes(void)
{
    std::vector<pinfo_t> processInfoList;
    std::string tmpFilename = createTempFileName();

    std::string cmd = "ps -eo pid,etimes,pcpu,pmem,drs,comm,args --no-headers  --sort=-pcpu";
    cmd += " > " + tmpFilename;

    system(cmd.c_str());

    std::ifstream inFile(tmpFilename);        
    std::string line;
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string pid, startTime, utilCPU, utilMem, memory, cmd, cmdArgs;
        if (!(iss >> pid 
                  >> startTime 
                  >> utilCPU
                  >> utilMem
                  >> memory
                  >> cmd
                  >> cmdArgs)) {  break; }

        // Get the cmdargs all the way to the end of the line
        std::size_t idx = line.rfind(cmdArgs);
        cmdArgs = line.substr(idx);

        std::istringstream arg(cmdArgs);
        std::vector<std::string> argList{std::istream_iterator<std::string>{arg},
                                         std::istream_iterator<std::string>{}};

        time_t timer;
        time(&timer);
        uint64_t currTime = (uint64_t) timer;

        /* Get the process' CPU runtime stats */
        uint64_t userTime, kernelTime;
        getUsageTime(pid, userTime, kernelTime);

        pinfo_t p;
        p.pid = (uint64_t) std::atof(pid.c_str());
        p.name = cmd;
        p.args = argList;
        p.start_time = currTime - ((uint64_t) std::atoi(startTime.c_str()));
        p.cpu_usage_user = userTime;
        p.cpu_usage_system = kernelTime;
        p.memory_usage = (uint64_t) std::atof (memory.c_str());
        p.cpu_utilization = (uint8_t) std::atof (utilCPU.c_str());
        p.memory_utilization = (uint8_t) std::atof (utilMem.c_str());
        processInfoList.push_back(p);
    }
    
    deleteTempFile(inFile, tmpFilename);
    return processInfoList;
}


static int populate_processes(struct sockaddr_in addr)
{
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    struct confd_datetime dt;
    int sock;
    FILE *proc;
    confd_value_t timeval;

    dt.year = tm->tm_year + 1900;
    dt.month = tm->tm_mon + 1;
    dt.day = tm->tm_mday; dt.hour = tm->tm_hour;
    dt.min = tm->tm_min; dt.sec = tm->tm_sec;
    dt.micro = 0; dt.timezone = CONFD_TIMEZONE_UNDEF;
    CONFD_SET_DATETIME(&timeval, dt);

    if ((sock = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        confd_fatal("Failed to create socket");
    }

    OK(cdb_connect_name(sock, CDB_DATA_SOCKET, (struct sockaddr *)&addr, sizeof(struct sockaddr_in), "system_processes_state_monitor"));
    OK(cdb_start_session(sock, CDB_OPERATIONAL));
    OK(cdb_set_namespace(sock, oc_sys__ns));

    /*
     * Get all processes on the NOS
     */
    std::vector<pinfo_t> processList = get_system_processes();
    std::vector<pinfo_t>::iterator it;
    for (it = processList.begin(); it != processList.end(); it++) {
        if (!cdb_exists(sock, "/system/processes/process{%u}", it->pid)) {
            OK(cdb_create(sock, "/system/processes/process{%u}", it->pid)); 
        }

        OK(cdb_cd(sock, "/system/processes/process{%u}/state", it->pid)); 
        
        confd_value_t val;

        CONFD_SET_UINT64(&val, it->pid);
        OK(cdb_set_elem(sock, &val, "pid"));

        CONFD_SET_STR(&val, (it->name).c_str());
        OK(cdb_set_elem(sock, &val, "name"));

        if (!cdb_exists(sock, "args")) { 
            OK(cdb_create(sock, "args"));
        }

        confd_value_t v[1];
        std::vector<std::string>::iterator i;
        std::string args;
        for (i = it->args.begin(); i != it->args.end(); i++) {
            args += *i + " ";
        }

        CONFD_SET_STR(&v[0], args.c_str());
        CONFD_SET_LIST(&val, &v[0], 1);
        OK(cdb_set_elem(sock, &val, "args"));

        CONFD_SET_UINT64(&val, it->start_time);
        OK(cdb_set_elem(sock, &val, "start-time"));

        CONFD_SET_UINT64(&val, it->cpu_usage_user);
        OK(cdb_set_elem(sock, &val, "cpu-usage-user"));

        CONFD_SET_UINT64(&val, it->cpu_usage_system);
        OK(cdb_set_elem(sock, &val, "cpu-usage-system"));

        CONFD_SET_UINT8(&val, it->cpu_utilization);
        OK(cdb_set_elem(sock, &val, "cpu-utilization"));

        CONFD_SET_UINT64(&val, it->memory_usage);
        OK(cdb_set_elem(sock, &val, "memory-usage"));

        CONFD_SET_UINT8(&val, it->memory_utilization);
        OK(cdb_set_elem(sock, &val, "memory-utilization"));
    }

    OK(cdb_end_session(sock));
    OK(cdb_close(sock));
    return CONFD_OK;
}

int main(int argc, char **argv)
{
    int interval = 0;
    struct sockaddr_in addr;

    if (argc > 1)
        interval = atoi(argv[1]);
    if (interval == 0)
        interval = INTERVAL;

    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CONFD_PORT);

    confd_init(argv[0], stderr, CONFD_TRACE);
    OK(confd_load_schemas((struct sockaddr*)&addr, sizeof(struct sockaddr_in)));

    while (1) {
        OK(populate_processes(addr));
        sleep(interval);
    }
}

