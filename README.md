[This file by Russian](ru/README.md)

# glonassd
GPS/GLONASS tracker server for Debian

### About
**glonassd** is a linux daemon that receives data from a GPS / GLONASS trackers, processing and preserving them in a database.<br>
Written in C, сompiled with gcc 6.3.0 for x86_64-linux-gnu.

### Tracker protocols
**Receiving:** Arnavi-4/5, Galileo (all versions), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Olympstroy), EGTS (ERA-GLONASS), TQ GPRS (H02).<br>
**Sending (forwarding):** all receiving without reencode or reencode to EGTS, Wialon NIS, WialonIPS, Galileo.<br>
Protocols can be added using plug libraries.

### Database
PostgreSQL<br>
Redis<br>
Oracle<br>
Databases can be added using plug libraries.

### Features
* Data transfer on the fly to another server with transcoding to a different protocol (to Wialon NIS or EGTS only) or without transcoding.
* Automatic restoration of connections to the remote server when the connection is broken.
* Storing data at the time of connection failure with the remote server and sending the stored data after the restoration of communication.
* The maximum number of relay servers: 3 for each terminal.
* Perform scheduled tasks (maximum 5 timers).
* Easy configuration using two .conf files (Examples: [glonassd.conf](https://github.com/fandrej/glonassd/wiki/glonassd.conf), [forward.conf](https://github.com/fandrej/glonassd/wiki/forward.conf))
* Extensibility through plug libraries without recompilation daemon.
* Run in daemon or simple application mode

### Installation
#### Preparation
```
sudo apt update & sudo apt upgrade
sudo apt install build-essential libpq-dev
```

It is supposed to be installed in the /opt/glonassd folder
```
cd /opt
git clone https://github.com/fandrej/glonassd.git
cd glonassd
make min
```

### Compilation
**make all** for compile daemon + databases library (Postgresql + Redis + Oracle) + terminals libraries<br>
**make min** for compile daemon + database library Postgresql + terminals libraries<br>
**make glonassd** for compile daemon only<br>
**make pg** for compile database (PostgreSQL) library<br>
**make name** for compile terminal **name** library<br>

[Additional information about threed party libraries](https://github.com/fandrej/glonassd/wiki/Compilation)

### Configuration
Create directories forward & logs: `mkdir -p forward logs`.<br>
In **glonassd.conf** file in **server** section edit values for:<br>
**listen** - IP addres listen trackers interface<br>
**transmit** - IP addres retranslation to remote server interface<br>
**log_file** - full path to log file<br>
**db_host, db_port, db_name, db_schema, db_user, db_pass** - parameters for you PostgreSQL database<br>
Comment or uncomment terminals sections for used terminals and edit listeners ports.

For forwarding terminals data to remote server see comments in **forward** section of the **glonassd.conf** file.<br>
For schedule database tasks see comments about **timer** parameter in **server** section of the **glonassd.conf** file.

### Check the POSIX message queue size limits
Test the system message queue length limit using `ulimit -q` or `ulimit -a` and check the value of the 'POSIX message queue'.
By default, it is 819200 bytes. Increase this value at least to 81920000.
To do this, add the following lines to the /etc/security/limits.conf file:
```
root    soft    msgqueue        81920000
root    hard    msgqueue        81920000
*       soft    msgqueue        81920000
*       hard    msgqueue        81920000
```
and reboot the system.
If the message "mq_send(config->db_queue) message queue is already full" appears in the log file during operation, then the queue size must be increased further.

### Run
From daemon folder use **sudo ./glonassd start** command for start in console mode, CTRL+C for stop.<br>
Use -d parameter for start in daemon mode.<br>
Use -c path/to/config/file parameter for config file not in daemon folder.<br>
If daemon configured as automatically startup, use **service glonassd start** and **service glonassd stop** for start/stop if needded.

### Autostart configure
#### As daemon
Edit **DAEMON** variable in **glonassd.sh** file for correct path to daemon folder.<br>
Copy **glonassd.sh** file in **/etc/init.d** folder.<br>
Use **chmod 0755 /etc/init.d/glonassd.sh** for make it executable.<br>
Use **systemctl daemon-reload** and **update-rc.d glonassd.sh defaults** for enable autostart daemon.<br>
Use **update-rc.d -f glonassd.sh remove** for diasble autostart without delete glonassd.sh file.<br>
Delete /etc/init.d/glonassd.sh file and use **systemctl daemon-reload** for fully cleanup daemon info.
#### Through the supervisor
```
sudo apt install supervisor
sudo ln -s $(pwd)/glonassd.supervisor.conf /etc/supervisor/conf.d/glonassd.supervisor.conf
sudo supervisorctl reread
sudo supervisorctl update glonassd
sudo supervisorctl status
```

### License
The glonassd is open-sourced software licensed under the [MIT license](http://opensource.org/licenses/MIT).

### Documentation and API
[Documentation](https://github.com/fandrej/glonassd/wiki) to be written.

### Author
Andrey Fedorov, Kurgan, Russia.<br>
<mailto:mail@locman.org>

### Epilog
This daemon is part of navigation service of [locman.org](http://locman.org/map/index.php).

### Fixes
12.06.2020<br>
* Fixed receiving big data (from other servers)
* EGTS protocol fixed
* CPU usage improved
* Redis database added

01.05.2021<br>
* Added TQ GPRS (H02) receiver protocol (SinoTrack ST-901 device)
* Added WialonIPS retranslation protocol
* Added Galileo retranslation protocol
* Small fixes

13.11.2021<br>
* Added library for working with Oracle database
* Improved logs in databases libraries

04.04.2022<br>
* Added processing of CT_MSGTO, CT_MSGFROM EGTS_COMMANDS_SERVICE messages of the EGTS protocol

17.04.2022<br>
* Added GoSafe protocol (Gosafe G*/Proma Sat G* devices)
