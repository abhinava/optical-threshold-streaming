from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager
import xml.etree.ElementTree as ET
from prometheus_client import start_http_server, Gauge, Counter
import signal

PROM_SERVER_URL = 'http://localhost:10000'
PROM_TOMBSTONE_CLEANUP = 10000
PROM_METRIC_NAME_PREFIX = 'ofc_2020_demo'

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
        self.LA_1MIN = Gauge(name + '_cpu_load_avg_1min', 'Load Average{1-min}')
        self.LA_5MIN = Gauge(name + '_cpu_load_avg_5min', 'Load Average{5-min}')
        self.LA_15MIN = Gauge(name + '_cpu_load_avg_15min','Load Average{15-min}')
        self.LA_NOTIF_COUNT = Counter(name + '_num_load_avg_events', 'Number of Load Avg Notifications')

    
    def SetLoadAverage1Min(self, val: float) -> None:
        self.LA_1MIN.set(val)


    def SetLoadAverage5Min(self, val: float) -> None:
        self.LA_5MIN.set(val)


    def SetLoadAverage15Min(self, val: float) -> None:
        self.LA_15MIN.set(val)

    def SetNotificationCount(self, val: int) -> None:
        self.LA_NOTIF_COUNT.inc(val)

def HandleProcessKill(signum, frame):
    print("Quitting! Hope you enjoyed!")
    
    import sys
    sys.exit(-1)
 

def main():
    start_http_server(54545)
    name = PROM_METRIC_NAME_PREFIX
    server = 'localhost' 

    signal.signal(signal.SIGINT, HandleProcessKill)
    signal.signal(signal.SIGTERM, HandleProcessKill)
    signal.signal(signal.SIGQUIT, HandleProcessKill)
    signal.signal(signal.SIGHUP, HandleProcessKill)

    import sys
    if len(sys.argv) > 1:
        name = sys.argv[1]

    if len(sys.argv) > 2:
        server = sys.argv[2]

    loadAverage = SystemLoadAverage(name=name)

    session = connect_ssh(host=server,
                          port=2022,
                          username='admin',
                          password='admin')
    mgr = Manager(session, timeout=120)
    filter='''
            <filter>
               <oc-proc-ext:system-load-average  xmlns:oc-proc-ext="http://infinera.com/yang/openconfig/system/procmon-ext"/>
            </filter>
           '''
    mgr.create_subscription(stream='threshold-stream', filter=filter)
    n = None
    notifCount = 0

    while True:
        n = mgr.take_notification(True)
        xml = n.notification_xml.decode('UTF-8')
        root = ET.fromstring(xml)

        notifCount = notifCount + 1
        print("1-Min Load Average: {}".format(root[1][0].text))
        print("5-Min Load Average: {}".format(root[1][1].text))
        print("15-Min Load Average: {}".format(root[1][2].text))
        print("Notifications received so far: {}".format(notifCount))
        print('*' * 100)

        loadAverage.SetLoadAverage1Min(val=float(root[1][0].text))
        loadAverage.SetLoadAverage5Min(val=float(root[1][1].text))
        loadAverage.SetLoadAverage15Min(val=float(root[1][2].text))
        loadAverage.SetNotificationCount(val=int(notifCount))

if __name__ == "__main__":
    main()
