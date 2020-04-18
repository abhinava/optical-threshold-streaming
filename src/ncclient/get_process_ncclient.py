from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager
import xml.etree.ElementTree as ET
from prometheus_client import start_http_server, Gauge, Info

PROM_SERVER_URL = 'http://localhost:10000'
PROM_TOMBSTONE_CLEANUP = 10000

############################################################################## 

PROCESS_PID_KEY = 'PID'
PROCESS_NAME_KEY = 'PROC_NAME'
PROCESS_CPU_UTIL_KEY = 'PROC_CPU_UTIL'
PROCESS_MEM_UTIL_KEY = 'PROC_MEM_UTIL'
PROCESS_START_TIME_KEY = 'PROC_START_TIME'
PROCESS_CPU_USER_KEY = 'PROC_CPU_USER'
PROCESS_CPU_KERNEL_KEY = 'PROC_CPU_KERNEL'

############################################################################## 

class SystemCPUMemoryLoad:
    def __init__(self, name):
        self.name = name
        self.CPU = Gauge(name + '_system_cpu_util', 'System CPU Utilization')
        self.MEM = Gauge(name + '_system_mem_util', 'System Memory Utilization')

    def SetTotalCPUUtilization(self, val: str) -> None:
        self.CPU.set(val)


    def SetTotalMemoryUtilization(self, val: str) -> None:
        self.MEM.set(val)


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
                      

def main():
    session = connect_ssh(host='localhost',
                          port=2022,
                          username='admin',
                          password='admin')

    start_http_server(54546)
    name = None
 
    cleanUpCount = PROM_TOMBSTONE_CLEANUP

    import sys
    if len(sys.argv) > 1:
        name = sys.argv[0]
    else:
        name = "ofc_2020_demo"

    processSet = set()
    cpuMem = SystemCPUMemoryLoad(name=name) 
    processPM = ProcessPM(prefix=name)

    mgr = Manager(session, timeout=120)
    mgr.create_subscription(stream='threshold-stream')
    n = None

    while True:
        n = mgr.take_notification(True)
        xml = n.notification_xml.decode('UTF-8')
        root = ET.fromstring(xml)
        if str(root[1].tag).find('system-overall-cpu-memory') != -1:
            print("Total CPU Utilization: {}".format(root[1][0].text))
            print("Total Memory Utilization: {}".format(root[1][1].text))
            cpuMem.SetTotalCPUUtilization(float(root[1][0].text))
            cpuMem.SetTotalMemoryUtilization(float(root[1][1].text))
        elif str(root[1].tag).find('process-statistics') != -1:
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


            print("No. of new set of processes: {}".format(len(processSet)))
            # Clean-up stale processes
            diff = processSet.difference(newProcessSet)
            print("No. of processes that need to be cleaned-up: {}".format(len(diff)))
            
            if len(diff) > 0:
                print(diff)

            processSet = newProcessSet 
            print("No. of effective set of processes: {}".format(len(processSet)))
            print('*' * 100)

            import requests
            for pid, pName in diff:
                res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/delete_series?match[]=ofc_2020_demo_process_cpu_total{PID="' + str(pid) + '"}')
                cleanUpCount = cleanUpCount - 1
            
            if cleanUpCount <= 0:
                res = requests.post(PROM_SERVER_URL + '/api/v1/admin/tsdb/clean_tombstones')
                print(res)
                cleanUpCount = PROM_TOMBSTONE_CLEANUP


if __name__ == "__main__":
    main()
