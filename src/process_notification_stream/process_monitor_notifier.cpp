/**
 * process_monitor_notifer.cpp
 *
 * Monitors the NOS and all running processes/threads
 * and emits periodic notification containing 
 * per-process statistics for all the processes.
 *
 * Abhinava Sadasivarao
 * (c) Infinera Corporation, 2020
 */
#include <cstring>
// #include <cstdlib>
#include <ctime>
#include <cmath>
// #include <algorithm>
#include <iterator>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
// #include <signal.h>

#include <confd_lib.h>
#include <confd_dp.h>
#include <confd_cdb.h>

#include "openconfig-procmon-ext.h"

#define INTERVAL 10
#define MAX_SAMPLES (86400/interval)

#define PROC_NAME 128
#define PROC_ARGS 1024

#define OK(rval) do {                                                   \
        if ((rval) != CONFD_OK)                                         \
            confd_fatal("error not CONFD_OK: %d : %s\n",                \
                        confd_errno, confd_lasterr());                  \
    } while (0);

static volatile int stream_interval = INTERVAL;
static volatile unsigned int prev_stream_interval = stream_interval;
static volatile float prev_demand = 0;

static unsigned int CPU_COUNT = 2;

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

struct load_avg_t {
    float load_avg_1min;
    float load_avg_5min;
    float load_avg_15min;
};


struct notif {
    struct confd_datetime eventTime;
    confd_tag_value_t *vals;
    int nvals;
};

/* The notification context (filled in by ConfD) for the live feed */
static struct confd_notification_ctx *live_ctx;
static struct confd_daemon_ctx *dctx;
static int ctlsock, workersock;

static int get_ctlsock(struct addrinfo *addr)
{
    int sock;

    if ((sock =
         socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0)
        return -1;
    if (confd_connect(dctx, sock, CONTROL_SOCKET,
                      addr->ai_addr, addr->ai_addrlen) != CONFD_OK) {
        close(sock);
        return -1;
    }
    return sock;
}

static int get_workersock(struct addrinfo *addr)
{
    int sock;

    if ((sock =
         socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol)) < 0)
        return -1;
    if (confd_connect(dctx, sock, WORKER_SOCKET,
                      addr->ai_addr, addr->ai_addrlen) != CONFD_OK) {
        close(sock);
        return -1;
    }
    return sock;
}

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

static void get_cpu_count(void)
{
    std::string tmpFilename = createTempFileName();
    std::string cmd = "grep processor /proc/cpuinfo | wc -l > " + tmpFilename;
    system(cmd.c_str());

    std::ifstream inFile(tmpFilename);        
    std::string line;
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string cpuCount;

        iss >> cpuCount;
        CPU_COUNT = (unsigned int) std::stoi(cpuCount);
        break;
    }

    std::cout << "The number of CPUs are: " << CPU_COUNT << std::endl;

    inFile.close();
    remove(tmpFilename.c_str());
}

static load_avg_t get_system_load_average(void)
{
    std::string tmpFilename = createTempFileName();
    std::string cmd = "cat /proc/loadavg > " + tmpFilename;
    system(cmd.c_str());

    load_avg_t loadAverages;
    
    /*
     * "cat /proc/loadavg" Output:
     * 0.29 0.50 0.48 1/1866 29717
     *
     * Output has a single line
     */

    std::ifstream inFile(tmpFilename);        
    std::string line;
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string load1Min, load5Min, load15Min;

        iss >> load1Min;
        iss >> load5Min;
        iss >> load15Min;

        loadAverages.load_avg_1min = std::stof(load1Min);
        loadAverages.load_avg_5min = std::stof(load5Min);
        loadAverages.load_avg_15min = std::stof(load15Min);
        break;
    }

    inFile.close();
    remove(tmpFilename.c_str());

    return loadAverages;
}

static void adapt_stream_interval(load_avg_t loadAverage)
{
    float loadAvg1min = loadAverage.load_avg_1min;
    float loadAvg5min = loadAverage.load_avg_5min;
    float loadAvg15min = loadAverage.load_avg_15min;


    if (loadAvg1min > loadAvg5min || loadAvg1min > loadAvg15min) {
        // Load is increasing

        float curr_demand = loadAvg1min / CPU_COUNT;
        if (curr_demand > 1.0) {
            // Overloaded
           
            if (curr_demand > prev_demand) {
                stream_interval = prev_stream_interval * 1.5;
            } else {
                stream_interval = (prev_stream_interval * 1.15);
            }
            prev_demand = curr_demand;
        }
        else {
            // Not yet overloaded
           stream_interval = (prev_stream_interval / 1.5); 
        }
    } else {
        // Load is decreasing
        // Reset to default interval
        stream_interval = INTERVAL;
    }

    std::cout << "Streaming Interval is: " << stream_interval << std::endl;
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
        // p.start_time = currTime - ((uint64_t) std::atoi(startTime.c_str()));
        p.start_time = (uint64_t) std::stoi(startTime);
        p.cpu_usage_user = userTime;
        p.cpu_usage_system = kernelTime;
        p.memory_usage = (uint64_t) std::stof (memory);
        p.cpu_utilization = (uint8_t) std::stof (utilCPU);
        p.memory_utilization = (uint8_t) std::stof(utilMem);
        processInfoList.push_back(p);

        // std::cout << "Memory %: " << p.memory_utilization << std::endl;
        // std::cout << "Memory (KiB): " << p.memory_usage << std::endl;
    }
    
    deleteTempFile(inFile, tmpFilename);
    return processInfoList;
}

static void getdatetime(struct confd_datetime *datetime)
{
    struct tm tm;
    struct timeval tv;

    gettimeofday(&tv, NULL);
    gmtime_r(&tv.tv_sec, &tm);

    memset(datetime, 0, sizeof(*datetime));
    datetime->year = 1900 + tm.tm_year;
    datetime->month = tm.tm_mon + 1;
    datetime->day = tm.tm_mday;
    datetime->sec = tm.tm_sec;
    datetime->micro = tv.tv_usec;
    datetime->timezone = 0;
    datetime->timezone_minutes = 0;
    datetime->hour = tm.tm_hour;
    datetime->min = tm.tm_min;
}

static void send_notification(std::vector<confd_tag_value_t> vals)
{
    struct confd_datetime now;
    getdatetime(&now);

    int sz = vals.size() * sizeof(confd_tag_value_t);
    confd_tag_value_t *elements = (confd_tag_value_t *) malloc (sz);

    for (int i = 0; i < vals.size(); i++) {
        confd_tag_value_t v = vals.at(i);
        memcpy((confd_tag_value_t *) &elements[i], (confd_tag_value_t *) &v, sizeof(confd_tag_value_t));
    }

    OK(confd_notification_send(live_ctx,
                               &now,
                               elements, 
                               (int) vals.size()));

    free (elements);
}


static int send_notif_process_statistics()
{
    std::vector<confd_tag_value_t> vals;
    std::vector<pinfo_t> processes = get_system_processes();

    confd_tag_value_t outer;
    CONFD_SET_TAG_XMLBEGIN(&outer, oc_proc_ext_process_statistics, oc_proc_ext__ns);
    vals.push_back(outer);

    float total_cpu_utilization = 0.0;
    float total_mem_utilization = 0.0;

    for (int i = 0; i < processes.size() ; i++) {
        confd_tag_value_t proc;
        CONFD_SET_TAG_XMLBEGIN(&proc,oc_proc_ext_process, oc_proc_ext__ns);
        vals.push_back(proc);

        confd_tag_value_t pid;
        CONFD_SET_TAG_UINT64(&pid, oc_proc_ext_pid, processes[i].pid);
        vals.push_back(pid);

        confd_tag_value_t name;
        CONFD_SET_TAG_STR(&name, oc_proc_ext_name, processes[i].name.c_str());
        vals.push_back(name);

        for (int j = 0; j < processes[i].args.size(); j++) {
            confd_tag_value_t t;
            CONFD_SET_TAG_CBUF(&t, oc_proc_ext_args, processes[i].args[j].c_str(), processes[i].args[j].size());
            // vals.push_back(t);
            // std::cout << "\tArg: " << CONFD_GET_CBUFPTR(CONFD_GET_TAG_VALUE(&t)) << std::endl;
        }

        confd_tag_value_t start_time;
        CONFD_SET_TAG_UINT64(&start_time, oc_proc_ext_start_time, processes[i].start_time);
        vals.push_back(start_time);

        confd_tag_value_t cpu_usage_user;
        CONFD_SET_TAG_UINT64(&cpu_usage_user, oc_proc_ext_cpu_usage_user, processes[i].cpu_usage_user);
        vals.push_back(cpu_usage_user);

        confd_tag_value_t cpu_usage_system;
        CONFD_SET_TAG_UINT64(&cpu_usage_system, oc_proc_ext_cpu_usage_system, processes[i].cpu_usage_system);
        vals.push_back(cpu_usage_system);

        total_cpu_utilization += processes[i].cpu_utilization;
        confd_tag_value_t cpu_utilization;
        CONFD_SET_TAG_UINT8(&cpu_utilization, oc_proc_ext_cpu_utilization, processes[i].cpu_utilization);
        vals.push_back(cpu_utilization);

        confd_tag_value_t memory_usage;
        CONFD_SET_TAG_UINT64(&memory_usage, oc_proc_ext_memory_usage, processes[i].memory_usage);
        // vals.push_back(memory_usage);
        // std::cout << "Memory Usage: " <<  CONFD_GET_UINT64(CONFD_GET_TAG_VALUE(&memory_usage)) << std::endl;

        total_mem_utilization += processes[i].memory_utilization;
        confd_tag_value_t memory_utilization;
        CONFD_SET_TAG_UINT8(&memory_utilization, oc_proc_ext_memory_utilization, processes[i].memory_utilization);
        vals.push_back(memory_utilization);

        CONFD_SET_TAG_XMLEND(&proc, oc_proc_ext_process, oc_proc_ext__ns);
        vals.push_back(proc);
    }

    CONFD_SET_TAG_XMLEND(&outer, oc_proc_ext_process_statistics, oc_proc_ext__ns);
    vals.push_back(outer);
    
    /* Emit the notification */
    send_notification(vals);

    // ----------- Total CPU and Memory ---------------

    std::cout << "CPU Utilization: " << total_cpu_utilization << std::endl;
    std::cout << "Memory Utilization: " << total_mem_utilization << std::endl;

    std::vector<confd_tag_value_t> cpu_memory_utilization;
    confd_tag_value_t cpuMemTag;
    CONFD_SET_TAG_XMLBEGIN(&cpuMemTag, oc_proc_ext_system_overall_cpu_memory, oc_proc_ext__ns);
    cpu_memory_utilization.push_back(cpuMemTag);

    struct confd_decimal64 cpuMem;
    cpuMem.value = total_cpu_utilization * pow(10, 2);
    cpuMem.fraction_digits = 2;
    CONFD_SET_TAG_DECIMAL64(&cpuMemTag, oc_proc_ext_cpu_utilization, cpuMem);
    cpu_memory_utilization.push_back(cpuMemTag);
    cpuMem.value = total_mem_utilization * pow(10, 2);
    cpuMem.fraction_digits = 2;
    CONFD_SET_TAG_DECIMAL64(&cpuMemTag, oc_proc_ext_memory_utilization, cpuMem);
    cpu_memory_utilization.push_back(cpuMemTag);

    CONFD_SET_TAG_XMLEND(&cpuMemTag, oc_proc_ext_system_overall_cpu_memory, oc_proc_ext__ns);
    cpu_memory_utilization.push_back(cpuMemTag);

    /* Emit the notification */
    send_notification(cpu_memory_utilization);

    adapt_stream_interval(get_system_load_average());

    return CONFD_OK;
}

int main(int argc, char **argv)
{
    char confd_port[16];
    int interval = 0;
    struct addrinfo *addr = NULL;
    struct addrinfo hints;
    struct confd_notification_stream_cbs ncb;
    char *p, *dname;

    if (argc > 1)
        interval = atoi(argv[1]);
    if (interval == 0)
        interval = INTERVAL;

    stream_interval = interval;

    // snprintf(confd_port, sizeof(confd_port), "%d", CONFD_PORT);
    snprintf(confd_port, sizeof(confd_port), "%d", 51015);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    confd_init(argv[0], stderr, CONFD_TRACE);

    int i = getaddrinfo("127.0.0.1", confd_port, &hints, &addr);
    if (i != 0) {
        confd_fatal("%s: Failed to get address for ConfD: %s\n", argv[0], gai_strerror(i));
    }

    if ((dctx = confd_init_daemon(argv[0])) == NULL)
        confd_fatal("Failed to initialize ConfD\n");
    if ((ctlsock = get_ctlsock(addr)) < 0)
        confd_fatal("Failed to connect to ConfD\n");
    if ((workersock = get_workersock(addr)) < 0)
        confd_fatal("Failed to connect to ConfD\n");

    memset(&ncb, 0, sizeof(ncb));
    ncb.fd = workersock;
    ncb.get_log_times = NULL;
    ncb.replay = NULL;
    strcpy(ncb.streamname, "threshold-stream");
    ncb.cb_opaque = NULL;

    if (confd_register_notification_stream(dctx, &ncb, &live_ctx) != CONFD_OK) {
        confd_fatal("Couldn't register stream %s\n", ncb.streamname);
    }
    if (confd_register_done(dctx) != CONFD_OK) {
        confd_fatal("Failed to complete registration\n");
    }

    get_cpu_count();

    while (1) {
        OK(send_notif_process_statistics());
        sleep(interval);
    }
}

