/**
 * load_avg_notifer.cpp
 *
 * Monitors the NOS system load average
 * and emits periodic notifications.
 *
 * Abhinava Sadasivarao
 * (c) Infinera Corporation, 2020
 */
#include <cstring>
#include <ctime>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>

#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/time.h>

#include <confd_lib.h>
#include <confd_dp.h>
#include <confd_cdb.h>

#include "openconfig-procmon-ext.h"

#define INTERVAL 30
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

struct load_avg_t {
    float load_avg_1min;
    float load_avg_5min;
    float load_avg_15min;
};

typedef struct load_avg_t load_avg_t;

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

static void get_cpu_count(void)
{
    std::string tmpFilename = createTempFileName();
    std::string cmd = "grep processor /proc/cpuinfo | wc -l > " + tmpFilename;
    system(cmd.c_str());

    std::ifstream inFile(tmpFilename.c_str()); //, std::ifstream::in);        
    std::string line;
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string cpuCount;

        iss >> cpuCount;
        CPU_COUNT = (unsigned int) atoi(cpuCount.c_str());
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

    std::ifstream inFile(tmpFilename.c_str()); //, std::ifstream::in); 
    std::string line;
    while (std::getline(inFile, line)) {
        std::istringstream iss(line);
        std::string load1Min, load5Min, load15Min;

        iss >> load1Min;
        iss >> load5Min;
        iss >> load15Min;

        loadAverages.load_avg_1min = atof(load1Min.c_str());
        loadAverages.load_avg_5min = atof(load5Min.c_str());
        loadAverages.load_avg_15min = atof(load15Min.c_str());
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
    // float loadAvg15min = loadAverage.load_avg_15min;

    prev_stream_interval = stream_interval;

    if (loadAvg1min > loadAvg5min) {
        // Load is increasing
        std::cout << "\tSystem Load is increasing...\n";

        float curr_demand = loadAvg1min; // / CPU_COUNT;
        if (curr_demand >= (0.4 * CPU_COUNT) && curr_demand < (0.41 * CPU_COUNT)) {
            std::cout << "\t\tSystem demand increasing and is "
                      << "currently greater than 40% of the number of CPU cores "
                      << "(" << CPU_COUNT << ")" << std::endl;

            float increase = 10; // 10%
            std::cout << "\t\tIncrease streaming frequency by "
                      << increase << "%" << std::endl;

            stream_interval = (prev_stream_interval / (1 + (increase/100))); 
            if (stream_interval <= 5) {
                stream_interval = rand() % 5;
            }
        }

        if (curr_demand >= (0.6 * CPU_COUNT) && curr_demand < CPU_COUNT) {
            std::cout << "\t\tSystem demand increasing and is "
                      << "currently at 60% of the number of CPU cores "
                      << "(" << CPU_COUNT << ")" << std::endl;

            float increase = 20; // 10%
            std::cout << "\t\tIncrease streaming frequency by "
                      << increase << "%" << std::endl;

            stream_interval = (prev_stream_interval / (1 + (increase/100))); 
            if (stream_interval <= 5) {
                stream_interval = rand() % 5;
            }
        }

        if (curr_demand > CPU_COUNT) {
            // Overloaded
            std::cout << "\t\tSystem demand is high and is currently "
                      << "at 50% of the number of CPU cores "
                      << "(" << CPU_COUNT << ")" << std::endl;
           
            if (curr_demand > prev_demand) {
                std::cout << "\t\t\tCurrent demand is greater than "
                          << "previous demand. Slowing down streaming by 50%"
                          << std::endl;

                stream_interval = prev_stream_interval * 1.5;
            } else {
                std::cout << "\t\t\tCurrent demand is lesser than previous "
                          << "demand. Slowing down streaming by a further 10%"
                          << std::endl;
                stream_interval = (prev_stream_interval * 1.10);
            }
            prev_demand = curr_demand;
        }
    } else {
        // Load is decreasing
        // Reset to default interval
        stream_interval = INTERVAL;
        std::cout << "\tSystem load is decreasing...\n";
    }

    std::cout << "Streaming Interval is: " << stream_interval << "s" << std::endl;
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

    for (int i = 0; i < (int) vals.size(); i++) {
        confd_tag_value_t v = vals.at(i);
        memcpy((confd_tag_value_t *) &elements[i], (confd_tag_value_t *) &v, sizeof(confd_tag_value_t));
    }

    OK(confd_notification_send(live_ctx,
                               &now,
                               elements, 
                               (int) vals.size()));

    free (elements);
}

static int send_notif_load_avg (void)
{
    std::vector<confd_tag_value_t> vals;
    load_avg_t loadAverages = get_system_load_average();

    confd_tag_value_t loadAvg;
    CONFD_SET_TAG_XMLBEGIN(&loadAvg, oc_proc_ext_system_load_average, oc_proc_ext__ns);
    vals.push_back(loadAvg);

    std::cout << "1-min: " << loadAverages.load_avg_1min << std::endl;
    std::cout << "5-min: " << loadAverages.load_avg_5min << std::endl;
    std::cout << "15-min: " << loadAverages.load_avg_15min << std::endl;

    confd_tag_value_t loadAvg1Min;
    struct confd_decimal64 min1;
    min1.value = loadAverages.load_avg_1min * pow(10, 2);
    min1.fraction_digits = 2;
    CONFD_SET_TAG_DECIMAL64(&loadAvg1Min, oc_proc_ext_avg_1_min, min1);
    vals.push_back(loadAvg1Min);

    confd_tag_value_t loadAvg5Min;
    struct confd_decimal64 min5;
    min5.value = loadAverages.load_avg_5min * pow(10, 2);
    min5.fraction_digits = 2;
    CONFD_SET_TAG_DECIMAL64(&loadAvg5Min, oc_proc_ext_avg_5_min, min5);
    vals.push_back(loadAvg5Min);

    confd_tag_value_t loadAvg15Min;
    struct confd_decimal64 min15;
    min15.value = loadAverages.load_avg_15min * pow(10, 2);
    min15.fraction_digits = 2;
    CONFD_SET_TAG_DECIMAL64(&loadAvg15Min, oc_proc_ext_avg_15_min, min15);
    vals.push_back(loadAvg15Min);

    CONFD_SET_TAG_XMLEND(&loadAvg, oc_proc_ext_system_load_average, oc_proc_ext__ns);
    vals.push_back(loadAvg);

    send_notification(vals);

    adapt_stream_interval(loadAverages);

    return CONFD_OK;
}

int main(int argc, char **argv)
{
    char confd_port[16];
    int interval = 0;
    struct addrinfo *addr = NULL;
    struct addrinfo hints;
    struct confd_notification_stream_cbs ncb;

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
        OK(send_notif_load_avg());
        sleep(stream_interval);
    }
}

