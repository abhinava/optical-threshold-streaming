from netconf_client.connect import connect_ssh
from netconf_client.ncclient import Manager

def main():
    session = connect_ssh(host='localhost',
                          port=2022,
                          username='admin',
                          password='admin')

    mgr = Manager(session, timeout=120)
    f = '<filter type="subtree"><oc-sys:system xmlns:oc-sys="http://openconfig.net/yang/system"><oc-sys:processes/></oc-sys:system></filter>'
    print(mgr.get(filter=f).data_xml)

    pass

if __name__ == "__main__":
    main()
