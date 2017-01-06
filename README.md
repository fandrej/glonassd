[This file by Russian](ru/README.md)

# glonassd
GPS/GLONASS tracker server for Debian

### About
**glonassd** is a linux daemon that receives data from a GPS / GLONASS trackers, processing and preserving them in a database.<br>
Written in C, сompiled with gcc 4.9.2 for x86_64-linux-gnu.

### Tracker protocols
**Receiving:** Arnavi, Galileo (all versions), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Olympstroy), EGTS (ERA-GLONASS).<br>
**Sending (forwarding):** all receiving without reencode or reencode to Wialon NIS or EGTS.<br>
Protocols can be added using plug libraries.

### Database
PostgreSQL<br>
Databases can be added using plug libraries.

### Features
* Data transfer on the fly to another server with transcoding to a different protocol (to Wialon NIS or EGTS only) or without transcoding.
* Automatic restoration of connections to the remote server when the connection is broken.
* Storing data at the time of connection failure with the remote server and sending the stored data after the restoration of communication.
* The maximum number of relay servers: 3 for each terminal.
* Perform scheduled tasks.
* Easy configuration using .conf files
* Extensibility through plug libraries without recompilation daemon.

### Compilation
**make all** for compile daemon + database library + terminals libraries<br>
**make glonassd** for compile daemon only<br>
**make pg** for compile database (PostgreSQL) library<br>
**make name** for compile terminal **name** library

### Installation
Create folder for daemon an copy in it files **glonassd, *.so, *.sql**, or use folder where project compiled.<br>
In you PostgreSQL database create table "tgpsdata" (see script tgpsdata.sql).<br>
If you use firewall, enable ports for incoming terminal connections.

### Configuration
In **glonassd.conf** file in **server** section edit values for:<br>
**listen** - IP addres listen trackers interface<br>
**transmit** - IP addres retranslation to remote server interface<br>
**log_file** - full path to log file<br>
**db_host, db_port, db_name, db_schema, db_user, db_pass** - parameters for you PostgreSQL database<br>
Comment or uncomment terminals sections for used terminals and edit listeners ports.

For forwarding terminals data to remote server see comments in **forward** section of the **glonassd.conf** file.<br>
For schedule database tasks see comments about **timer** parameter in **server** section of the **glonassd.conf** file.

### Run
From daemon folder use **./glonassd start** command for start daemon, **stop** | **restart** parameters for stop and restart daemon.<br>
Use -c path/to/config/file parameter for config file not in daemon folder.

### Autostart configure
Edit **DAEMON** variable in **glonassd.sh** file for correct path to daemon folder.<br>
Copy **glonassd.sh** file in **/etc/init.d** folder.<br>
Use **chmod 0755 /etc/init.d/glonassd.sh** for make it executable.<br>
Use **systemctl daemon-reload** and **update-rc.d glonassd.sh defaults** for enable autostart daemon.<br>
Use **update-rc.d -f glonassd.sh remove** for diasble autostart without delete glonassd.sh file.<br>
Delete /etc/init.d/glonassd.sh file and use **systemctl daemon-reload** for fully cleanup daemon info.

### License
The glonassd is open-sourced software licensed under the [MIT license](http://opensource.org/licenses/MIT).

### Documentation and API
Documentation to be written.

### Epilog
This daemon is part of navigation service of [locman.org](http://locman.org/map/index.php).<br>
Scold author can be reached at <mailto:mail@locman.org>.
