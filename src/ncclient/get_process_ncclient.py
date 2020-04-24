from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager
import xml.etree.ElementTree as ET
from prometheus_client import start_http_server, Gauge, Counter
import signal

PROM_SERVER_URL = 'http://localhost:10000'
PROM_TOMBSTONE_CLEANUP = 10000
PROM_METRIC_NAME_PREFIX = 'ofc_2020_demo'
PROM_METRIC_CLEANUP_SCRIPT = './prom_cleanup_script.sh'

############################################################################## 

PROCESS_PID_KEY = 'PID'
PROCESS_NAME_KEY = 'PROC_NAME'
PROCESS_CPU_UTIL_KEY = 'PROC_CPU_UTIL'
PROCESS_MEM_UTIL_KEY = 'PROC_MEM_UTIL'
PROCESS_START_TIME_KEY = 'PROC_START_TIME'
PROCESS_CPU_USER_KEY = 'PROC_CPU_USER'
PROCESS_CPU_KERNEL_KEY = 'PROC_CPU_KERNEL'

############################################################################## 

cleanupScriptFile = None
zombieProcessSet = dict()

class SystemCPUMemoryLoad:
    def __init__(self, name):
        self.name = name
        self.CPU = Gauge(name + '_system_cpu_util', 'System CPU Utilization')
        self.MEM = Gauge(name + '_system_mem_util', 'System Memory Utilization')
        self.NOTIF_COUNT = Counter(name + '_num_cpu_mem_events', 'Number of CPU + Memory Events')

    def SetTotalCPUUtilization(self, val: str) -> None:
        self.CPU.set(val)


    def SetTotalMemoryUtilization(self, val: str) -> None:
        self.MEM.set(val)


    def SetNotificationCount(self, val: int) -> None:
        self.NOTIF_COUNT.inc(val)


class ProcessPM:
    def __init__(self, prefix: str):
        self.NUM_PROCESSES = Gauge(name=prefix + '_num_active_processes',
                                   documentation="Total Number of Active Processes")

        self.CPU_TOTAL = Gauge(name=prefix + '_process_cpu_total',
                               documentation='Running Process CPU Utilization',
                               labelnames=[PROCESS_PID_KEY,
                                           PROCESS_NAME_KEY])
                          
        self.MEM_TOTAL = Gauge(name=prefix + '_process_mem_total',
                               documentation='Running Process Memory Utilization',
                               labelnames=[PROCESS_PID_KEY,
                                           PROCESS_NAME_KEY])

        self.START_TIME = Gauge(name=prefix + '_process_start_time',
                                documentation='Running Process Start Time',
                                labelnames=[PROCESS_PID_KEY,
                                            PROCESS_NAME_KEY])

        self.CPU_USER_TIME = Gauge(name=prefix + '_process_user_space_time',
                                   documentation='Time Spent in Userspace',
                                   labelnames=[PROCESS_PID_KEY,
                                               PROCESS_NAME_KEY])

        self.CPU_KERN_TIME = Gauge(name=prefix + '_process_kernel_space_time',
                                   documentation='Time Spent in Kernel Space',
                                   labelnames=[PROCESS_PID_KEY,
                                               PROCESS_NAME_KEY])

        self.STOP_TIME = Gauge(name=prefix + '_process_stop_time',
                                documentation='The time stamp when the process was killed/stopped',
                                labelnames=[PROCESS_PID_KEY,
                                            PROCESS_NAME_KEY])

        self.NOTIF_COUNT = Counter(prefix + '_num_proc_stat_events','Number of CPU + Memory Events')


    def SetNotificationCount(self, val: int) -> None:
        self.NOTIF_COUNT.inc(val)


    def NumActiveProcesses(self, val: int):
        self.NUM_PROCESSES.set(val)


    def SetStatistics(self,
                      pid: int,
                      name: str,
                      startTime: int,
                      utilCPU: float,
                      utilMem: float,
                      cpuUserTime: int,
                      cpuSysTime: int):

        self.CPU_TOTAL.labels(pid, name).set(utilCPU)
        self.MEM_TOTAL.labels(pid, name).set(utilMem)
        self.START_TIME.labels(pid, name).set(startTime)
        self.CPU_USER_TIME.labels(pid, name).set(cpuUserTime)
        self.CPU_KERN_TIME.labels(pid, name).set(cpuSysTime)

    def SetStopTime(self,
                    pid: int,
                    name: str,
                    stopTime: int):
        self.STOP_TIME.labels(pid, name).set(int(stopTime))
                      

def main():
    start_http_server(54546)
    name = None
    sever = None
 
    import sys
    if len(sys.argv) > 1:
        name = sys.argv[1]
    else:
        name = "ofc_2020_demo"

    global PROM_METRIC_NAME_PREFIX
    PROM_METRIC_NAME_PREFIX = name

    if len(sys.argv) > 2:
        server = sys.argv[2]
    else:
        server = "localhost"

    signal.signal(signal.SIGUSR2, HandleSignalUSR2)
    signal.signal(signal.SIGINT, HandleProcessKill)
    signal.signal(signal.SIGTERM, HandleProcessKill)
    signal.signal(signal.SIGQUIT, HandleProcessKill)
    signal.signal(signal.SIGHUP, HandleProcessKill)

    global zombieProcessSet, cleanupScriptFile
    cleanupScriptFile = PrometheusMetricCleanupScript()

    processSet = set()
    cpuMem = SystemCPUMemoryLoad(name=name) 
    processPM = ProcessPM(prefix=name)

    ######## Connect to the NETCONF Server ###########
    session = connect_ssh(host=server,
                          port=2022,
                          username='admin',
                          password='admin')

    mgr = Manager(session, timeout=120)
    filter = '''
              <filter>
                <oc-proc-ext:system-overall-cpu-memory xmlns:oc-proc-ext="http://infinera.com/yang/openconfig/system/procmon-ext"/>
                <oc-proc-ext:process-statistics xmlns:oc-proc-ext="http://infinera.com/yang/openconfig/system/procmon-ext"/>
              </filter>
             '''

    mgr.create_subscription(stream='threshold-stream',filter=filter)
    n = None
    notifCountCpuMem = 0
    notifCountProcessStats = 0

    while True:
        n = mgr.take_notification(True)
        xml = n.notification_xml.decode('UTF-8')
        root = ET.fromstring(xml)
        if str(root[1].tag).find('system-overall-cpu-memory') != -1:
            print("Total CPU Utilization: {}".format(root[1][0].text))
            print("Total Memory Utilization: {}".format(root[1][1].text))
            cpuMem.SetTotalCPUUtilization(float(root[1][0].text))
            cpuMem.SetTotalMemoryUtilization(float(root[1][1].text))
            notifCountCpuMem = notifCountCpuMem + 1
            cpuMem.SetNotificationCount(notifCountCpuMem)
        elif str(root[1].tag).find('process-statistics') != -1:
            notifCountProcessStats = notifCountProcessStats + 1
            processPM.SetNotificationCount(notifCountProcessStats)

            print("Total Number of Active Procsses: {}".format(len(root[1])))
            processPM.NumActiveProcesses(val=len(root[1]))
            processes = root[1]

            print("No. of exisitng processes: {}".format(len(processSet)))

            newProcessSet = set()
            for p in processes:
                pid = p[0].text
                pName = p[1].text
                startTime = p[2].text
                cpuUsageTotal = p[5].text
                memUsageTotal = p[6].text
                cpuUserTime = p[3].text
                cpuKernTime = p[4].text

                processSet.add((pid, pName))
                newProcessSet.add((pid, pName))

                processPM.SetStatistics(pid=int(pid),
                                        name=pName,
                                        startTime=int(startTime),
                                        utilCPU=float(cpuUsageTotal),
                                        utilMem=float(memUsageTotal),
                                        cpuUserTime=int(cpuUserTime), 
                                        cpuSysTime=int(cpuKernTime))

            diff = processSet.difference(newProcessSet)

            if len(diff) > 0:
                print(diff)

            for pid, pName in diff:
                import time
                stopTime = int(time.time()) - 10
                AppendZombieProcessPrometheusCleanup(cleanupScriptFile, pid, pName)
                zombieProcessSet[pid] = (pName,stopTime) 
                processPM.SetStopTime(pid=pid, name=pName, stopTime=stopTime)

            print(zombieProcessSet)

            print("No. of existing set of processes: {}".format(len(processSet)))
            print("No. of new set of processes: {}".format(len(newProcessSet)))
            print("No. of processes that need to be cleaned-up: {}".format(len(diff)))
            print("Total no. of zombie processes that are no longer alive: {}".format(len(zombieProcessSet)))
            print("No. of effective set of processes: {}".format(len(processSet)))
            print('*' * 100)

            processSet = newProcessSet 
    
        print("No. of overall-system-cpu-memory events received so far: {}".format(notifCountCpuMem))
        print("No. of process-statistics events received so far: {}".format(notifCountProcessStats))


def AppendZombieProcessPrometheusCleanup(f: object, pid: int, name: str) -> None:
    baseUrl = 'curl -w "%{http_code}" -X POST -g \'' + PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX
    f.write(baseUrl + '_process_cpu_total{PID="' + str(pid) + '", PROC_NAME="' + str(name) + '"}\'\n')
    f.write(baseUrl + '_process_mem_total{PID="' + str(pid) + '", PROC_NAME="' + str(name) + '"}\'\n')
    f.write(baseUrl + '_cpu_process_start_time{PID="' + str(pid) + '", PROC_NAME="' + str(name) +  '"}\'\n')
    f.write(baseUrl + '_cpu_process_stop_time{PID="' + str(pid) + '", PROC_NAME="' + str(name) + '"}\'\n')
    f.write(baseUrl + '_cpu_process_user_space_time{PID="' + str(pid) + '", PROC_NAME="' + str(name) + '"}\'\n') 
    f.write(baseUrl + '_cpu_process_kernel_space_time{PID="' + str(pid) + '", PROC_NAME="' + str(name) + '"}\'\n')
    f.flush()


def PrometheusMetricCleanupScript():
    import pathlib
    global PROM_METRIC_CLEANUP_SCRIPT
    file = pathlib.Path(PROM_METRIC_CLEANUP_SCRIPT)
    scriptFile = None
    if file.exists():
        scriptFile = open(PROM_METRIC_CLEANUP_SCRIPT, 'a')
    else:
        scriptFile = open(PROM_METRIC_CLEANUP_SCRIPT, 'w')
        scriptFile.write('#!/bin/bash')

    scriptFile.write('\n')
    return scriptFile


def HandleProcessKill(signum, frame):
    print("Quitting! Hope you enjoyed!")
    
    global cleanupScriptFile
    if cleanupScriptFile is not None:
        cleanupScriptFile.close()
    
    import sys
    sys.exit(-1)
 
def HandleSignalUSR2(signum, frame):
    import requests
    global zombieProcessSet
    print("Cleaning up {} (stale/dead) process metrics from Prometheus".format(len(zombieProcessSet)))
    cleanUpCount = 0

    for key in zombieProcessSet.keys():
        pid = key
        name = zombieProcessSet[key][0]
        stopTime = zombieProcessSet[key][1]

        status = dict()
        res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_process_cpu_total{PID="' + str(pid) + '"}')
        status['process_cpu_total'] = res

        res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_process_mem_total{PID="' + str(pid) + '"}')
        status['process_mem_total'] = res

        # res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_cpu_process_start_time{PID="' + str(pid) + '"}')
        # res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_cpu_process_stop_time{PID="' + str(pid) + '"}')
        res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_cpu_process_user_space_time{PID="' + str(pid) + '"}')
        status['process_user_space_time'] = res

        res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=' + PROM_METRIC_NAME_PREFIX + '_cpu_process_kernel_space_time{PID="' + str(pid) + '"}')
        status['process_kernel_space_time'] = res

        print("Deleting metrics for process PID: {} Name: {} Stopped Time: {}".format(pid, name, stopTime))
        print(status)
        cleanUpCount = cleanUpCount + 1
    
    zombieProcessSet.clear()
            
    if cleanUpCount >= PROM_TOMBSTONE_CLEANUP:
        res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/clean_tombstones')


if __name__ == "__main__":
    main()
