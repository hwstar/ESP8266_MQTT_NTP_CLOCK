**ESP8266-NTP-CLOCK**

A no-set clock using an ESP8266 module and a Sparkfun clock display. No other boards are required as this code runs on the ESP8266 natively. 
This project uses the Simple Network Time Protocol (SNTP) to get the time from a list of user specified time servers, and uses MQTT to set configuration options.  

![ProjectPicture](setuppic.jpg)

**Device Path**

The device path encompasses subtopics command and status. Commands are sent to $devicepath/command (which the nodes subscribes to.) All status messages are
published by the node on $devicepath/status except for power on message which is published on /node/info. the The device path is set using the patching procedure described later.


**Control Messages**

Control messages are received by all nodes on /node/control. These are meant to be used to interrogate the nodes connected to the network, 
and perform other system-wide control functions.

One control message is currently supported: *muster*. This directs the node to re-send the node configuration information to /node/info. See the power on message below for further details


**Command Messages**

Command messages are sent using JSON encoding as follows:

{"command":"command from table below"} For commands without a parameter

{"command":"$COMMAND","param","$PARAM"} For commands with a parameter

Because of limitations with the Espressif JSON parser library, all numbers should be sent as text fields 
(i.e. quoted)


|Command    | Description |
|-------    | ----------- |
|time24	    | Sets display format 1 = 24 hour, 0 = 12 hour|
|utcoffset	| Sets offset in seconds from UTC|
|survey	    | Returns WIFI survey information as seen by the node|
|ssid       | Query or set SSID|
|wifipass   | Query or set WIFI password|
|restart    | Restarts the clock

Notes:

* $ indicates a variable. e.g.: $COMMAND would be one of the commands in the table above.
* Sending an ssid, or wifipass command without "parameter":"$PARAM" will return the current value.
* ssid, wifipass change not effective until next system restart

Status messages which can be published:

* WIFI survey data in the following format: {"access_points":["$AP":{"chan":"$CHAN","rssi":"$RSSI"}...]} 

**MQTT Power on Message**

After booting, the node posts a JSON encoded message to /node/info with the following data:

|Field		| Description|
|-----      | -----------|
|connstate	| Indicates device is on line|
|device	    | A device path (e.g. /home/lab/clock)|
|ip4	    | The IP address assigned to the node|
|schema		| A schema name of hwstar_ntpclock (vendor_product)|
|ssid		| SSID in use


The schema may be used to design a database of supported commands for each device:

Here is an example:

{"muster":{"connstate":"online","device":"/home/lab/relay","ip4":"$IP","schema":"hwstar_ntpclock","ssid":"$SSID"}}


**Last Will and Testament**

The following will be published to /node/info if the node is not heard from by the MQTT broker:

{"muster":{"connstate":"offline","device":"$DEVICE"}}

**Configuration Patcher**

NB: WIFI and MQTT Configration is not stored in the source files. It is patched in using a custom Python utility which is available on my github account as
a separate project.The patcher allows the WWID, password, time zone, and other configuration options to be specified using a target in the Makefile.
The python script can be obtained here:
  
https://github.com/hwstar/ESP8266-MQTT-config-patcher

**Toolchain**

Requires the ESP8266 toolchain be installed on the Linux system per the instructions available here:

https://github.com/pfalcon/esp-open-sdk

toolchain should be installed in the /opt directory. Other directories will require Makefile modifications.

NB:Current Makefile supports Linux build hosts only at this time.

**Electrical Details**

Serial data is output on GPIO2 at 9600 baud 8 bits, 1 stop bit with no parity. The Sparkfun clock display is part number COM-11629.

LICENSE - "MIT License"

Copyright (c) 2015 Stephen Rodgers
 
MQTT code Copyright (c) 2014-2015 Tuan PM, https://twitter.com/TuanPMT

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
