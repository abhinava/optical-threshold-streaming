from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager
import xml.etree.ElementTree as ET
#from prometheus_client import start_http_server, Gauge

############################################################################## 

PROCESS_PID_KEY = 'PID'
PROCESS_NAME_KEY = 'PROC_NAME'
PROCESS_CPU_UTIL_KEY = 'PROC_CPU_UTIL'
PROCESS_MEM_UTIL_KEY = 'PROC_MEM_UTIL'
PROCESS_START_TIME_KEY = 'PROC_START_TIME'

############################################################################## 

'''
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
'''

def main():
    session = connect_ssh(host='localhost',
                          port=2022,
                          username='admin',
                          password='admin')

    process_list = dict()

    mgr = Manager(session, timeout=120)
    mgr.create_subscription(stream='threshold-stream')
    n = None

    while True:
        n = mgr.take_notification(True)
        xml = n.notification_xml.decode('UTF-8')
        root = ET.fromstring(xml)
        for child in root:
            print(child.tag)
            for c in child:
                items = c.items()
                for item in items:
                    print(items)

            '''
            for c in child:
                # if str(c.tag).find('load-average') != -1:
                print('\t{}'.format(c.tag))
                items = c.items()
                print('\t\t{}'.format(items))
            '''

if __name__ == "__main__":
    main()
