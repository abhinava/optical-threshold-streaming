# Threshold Based Streaming Telemetry for Optical Network Elements

This project implements streaming telemetry based performance monitoring (PM) using NETCONF notifications ([RFC 5277](https://tools.ietf.org/html/rfc5277)). Conventional "_all-you-can-eat_" streaming telemetry (since 2014) has seen [increasing adoption](https://www.osapublishing.org/abstract.cfm?uri=OFC-2018-Tu3D.3) and is steadily replacing SNMP-based monitoring. Streaming telemetry is now available on several networking platforms (routing, optical transport) which uses [gRPC](https://grpc.io) as the streaming wire protocol. Open interface definitions such as [gNMI](https://github.com/openconfig/reference/blob/master/rpc/gnmi/gnmi-specification.md) introduce gRPC service definitions that can be used across different vendor implementations of streaming telemetry to standardize a common set of telemetry operations.

In this project, we showcase:
1. A practical extension called **_threshold-based streaming telemetry_** which extends conventional streaming telemetry. In gRPC/gNMI telemetry, clients can specify (as part of [subscription creation](https://github.com/openconfig/reference/blob/master/rpc/gnmi/gnmi-specification.md#35-subscribing-to-telemetry-updates)) a [`sample_interval`](https://github.com/openconfig/reference/blob/master/rpc/gnmi/gnmi-specification.md#35152-stream-subscriptions) which indicates the frequency at which the network element should emit the PM data.
	 - One of the issues with this mechanism, is that the streaming frequency is fixed regardless of the underlying system state.
   - With threshold-based streaming telemetry, the network element (NE) adaptively changes the rate of streaming (stream faster or slower) depending upon the (contextually) parameter being monitored. For example, in our demonstration, we automatically adapt the streaming frequency of the optical NE controller's CPU utilization PM parameter, based on the [system load average](http://www.brendangregg.com/blog/2017-08-08/linux-load-averages.html) variance.
2. We use NETCONF as the streaming wire protocol as opposed to gRPC
   - NETCONF is widely used as a configuration protocol and is supported on most networking platforms (routing to optical transport).
   - NETCONF also supports asynchronous notifications ([RFC 5277](https://tools.ietf.org/html/rfc5277)) which pre-dates gRPC. Clients can use the [`<create-subscription>`](https://tools.ietf.org/html/rfc5277#section-2.1.1) NETCONF native RPC to subscribe to event streams of interest. The NE can support one or more streams on which data (PM, alarms, events) are published.
   - NETCONF always operates over SSH which provides security (similar to TLS in case of gRPC).

## Implementation Details

This project uses Cisco/Tail-F [ConfD](https://developer.cisco.com/site/confD/) as the NETCONF stack to implement the threshold-based streaming notifications. We use [OpenConfig](https://openconfig.net/) YANG models (with extensions) to represent the PM data. Specifically, we stream the following optical NE controller's operating system level PM parameters:
  - CPU Utilization [reference](https://github.com/openconfig/public/blob/master/release/models/system/openconfig-system.yang#L963))
  - Memory Utilization ([reference](https://github.com/openconfig/public/blob/master/release/models/system/openconfig-system.yang#L847))
  - System Load Averages (YANG extension - OpenConfig doesn't support load averages)
  - Per-process Statistics ([reference](https://github.com/openconfig/public/blob/master/release/models/system/openconfig-procmon.yang#L71))
  
Threshold based streaming of each of the above PM parameters is independently performed by a separate OS process (on the optical NE). The ConfD stack runs as a separate set of processes implementing the NETCONF protocol. Using ConfD's IPC mechanism (`libconfd.so`), each of the PM streaming processes connect to ConfD, and write data to the NETCONF operational data store (as well as publish to a dedicated NETCONF notification stream) which is then available to the telemetry clients.The ConfD stack and the streaming processes all run on the optical NE's operating system.
  
This project also has Python [ncclient](https://pypi.org/project/ncclient/) based NETCONF client implementation to subscribe to the threshold PM telemetry data from the optical NE.

## Demonstration

This project was done as a collaboration between **Infinera** and **Oracle Cloud Infrastructure (OCI)**, as part of [OFC 2020 Demo Zone](https://www.osapublishing.org/conference.cfm?meetingid=5&yr=2020):

- Abhinava Sadasivarao, Sharfuddin Syed, Deepak Panda, Paulo Gomes, Rajan Rao, Jonathan Buset, Loukas Paraschis, Jag Brar, and Kannan Raj, _"**Demonstration of Extensible Threshold-based Streaming Telemetry for Open DWDM Analytics and Verification**"_, Optical Fiber Conference (OFC) 2020 [(DOI)](https://doi.org/10.1364/OFC.2020.M3Z.5).
  - The demonstration was done on **Infinera's** [**XT-3300**](https://www.infinera.com/products/xt-series) **optical transponder**. The NETCONF threshold based streaming applications were run as "[software agents](https://www.osapublishing.org/abstract.cfm?uri=OFC-2019-M3Z.1)" on the host XT-3300 NE operating system.
  - The [poster](https://www.osapublishing.org/abstract.cfm?URI=OFC-2020-M3Z.5#articleSupplMat) provides additional details on the actual demonstration setup, including the data collection and visualization (using [Prometheus](https://prometheus.io/) and [Grafana](https://grafana.com/)).

# Other

 - To download and install ConfD, please visit [this](https://developer.cisco.com/site/confD/downloads/) page.
    - Although the link above refers to `confd-basic`, we have tested our streaming agents with [ConfD Premium](https://www.tail-f.com/management-agent/) and it works just as well.
 - ConfD requires OpenSSL's **libcrypto**, _specifically_, `libcrypto.so.1.0.0`. Newer versions of libcrypto may not be (historically) compatible with ConfD. Please refer to the ConfD user guide for details.
    - Installation of libcrypto is out-of-scope of this guide. Refer to your operating system (preferably Linux) distribution for details.
    - _If using Linux, one could use popular distributions such as Debian to [obtain](https://packages.debian.org/search?suite=jessie&arch=any&mode=filename&searchon=contents&keywords=libcrypto.so.1.0.0) the `libcrypto.so.1.0.0` library_.

--------
**(c) Infinera Corporation, 2020**

[Abhinava Sadasivarao](mailto:ASadasivarao@infinera.com)
