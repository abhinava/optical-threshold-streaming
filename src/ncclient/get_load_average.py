from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager
import xml.etree.ElementTree as ET
from prometheus_client import start_http_server, Gauge

############################################################################## 

PROCESS_PID_KEY = 'PID'
PROCESS_NAME_KEY = 'PROC_NAME'
PROCESS_CPU_UTIL_KEY = 'PROC_CPU_UTIL'
PROCESS_MEM_UTIL_KEY = 'PROC_MEM_UTIL'
PROCESS_START_TIME_KEY = 'PROC_START_TIME'

############################################################################## 

CPU_LOAD_AVG_1MIN = 'CPU_LA_1MIN'
CPU_LOAD_AVG_5MIN = 'CPU_LA_5MIN'
CPU_LOAD_AVG_15MIN = 'CPU_LA_15MIN'

############################################################################## 

class SystemLoadAverage:
    def __init__(self, name: str):
        self.name = name
        self.LA_1MIN = Gauge(name + '_' + 'cpu_load_avg_1min', 'Load Average{1-min}')
        self.LA_5MIN = Gauge(name + '_' + 'cpu_load_avg_5min', 'Load Average{5-min}')
        self.LA_15MIN = Gauge(name + '_' + 'cpu_load_avg_15min','Load Average{15-min}')

    
    def SetLoadAverage1Min(self, val: float) -> None:
        self.LA_1MIN.set(val)


    def SetLoadAverage5Min(self, val: float) -> None:
        self.LA_5MIN.set(val)


    def SetLoadAverage15Min(self, val: float) -> None:
        self.LA_15MIN.set(val)


class ProcessPM:
    def __init__(self, PID, name, startTime):
        self.PID = PID
        self.name = name
        self.startTime = startTime

        self.CPU_UTIL = Gauge(PID + '_' + name + '_' + 'cpu_util' +  '{CPU Utilization}')
        self.MEM_UTIL = Gauge(PID + '_' + name + '_' + 'mem_util' +  '{Memory Utilization}')
        self.CPU_USAGE_USER = Gauge(PID + '_' + name + '_' + 'cpu_util_user' +  '{User Space CPU Utilization}')
        self.CPU_USAGE_SYSTEM = Gauge(PID + '_' + name + '_' + 'cpu_util_sys' +  '{Kernel Space CPU Utilization}')

    def SetCPUUtilization(self, val):
        self.CPU_UTIL.set(val)

    def SetMemUtilization(self, val):
        self.MEM_UTIL.set(val)

    def SetCPUUserUtilization(self, val):
        self.CPU_USAGE_USER.set(val)

    def SetCPUSystemtilization(self, val):
        self.CPU_USAGE_SYSTEM.set(val)

    def GetPID(self):
        return self.PID

    def GetName(self):
        return self.name

    def GetStartTime(self):
        return self.startTime


def main():
    session = connect_ssh(host='localhost',
                          port=2022,
                          username='admin',
                          password='admin')

    start_http_server(54545)
    name = None

    import sys
    if len(sys.argv) > 1:
        name = sys.argv[0]
    else:
        name = "ofc_2020_demo"

    loadAverage = SystemLoadAverage(name=name)

    mgr = Manager(session, timeout=120)
    mgr.create_subscription(stream='threshold-stream')
    n = None

    while True:
        n = mgr.take_notification(True)
        xml = n.notification_xml.decode('UTF-8')
        root = ET.fromstring(xml)
        if str(root[1].tag).find('system-load-average') == -1:
            continue

        print("1-Min Load Average: {}".format(root[1][0].text))
        print("5-Min Load Average: {}".format(root[1][1].text))
        print("15-Min Load Average: {}".format(root[1][2].text))
        print('*' * 100)

        loadAverage.SetLoadAverage1Min(val=float(root[1][0].text))
        loadAverage.SetLoadAverage5Min(val=float(root[1][1].text))
        loadAverage.SetLoadAverage15Min(val=float(root[1][2].text))


if __name__ == "__main__":
    main()
