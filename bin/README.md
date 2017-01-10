### English
Acrchive contain worked binary version of the daemon, compiled with gcc version 4.9.2 (Debian 4.9.2-10) for Debian 3.16.36-1+deb8u2 (2016-10-19) x86_64 GNU/Linux.

### Russian
Архив содержит скомпилированную gcc version 4.9.2 (Debian 4.9.2-10) для Debian 3.16.36-1+deb8u2 (2016-10-19) x86_64 GNU/Linux версию демона, готовую к работе.

### Installation
Create folder for daemon an copy in it files **glonassd, *.so, *.sql**, or use folder where project$
In you PostgreSQL database create table "tgpsdata" (see script tgpsdata.sql).<br>
If you use firewall, enable ports for incoming terminal connections.

### Configuration
In **glonassd.conf** file in **server** section edit values for:<br>
**listen** - IP addres listen trackers interface<br>
**transmit** - IP addres retranslation to remote server interface<br>
**log_file** - full path to log file<br>
**db_host, db_port, db_name, db_schema, db_user, db_pass** - parameters for you PostgreSQL database$
Comment or uncomment terminals sections for used terminals and edit listeners ports.

For forwarding terminals data to remote server see comments in **forward** section of the **glonass$
For schedule database tasks see comments about **timer** parameter in **server** section of the **g$

### Run
From daemon folder use **./glonassd start** command for start daemon, **stop** | **restart** parame$
Use -c path/to/config/file parameter for config file not in daemon folder.<br>
If daemon configured as automatically startup, use **service glonassd start** and **service glonass$

### Autostart configure
Edit **DAEMON** variable in **glonassd.sh** file for correct path to daemon folder.<br>
Copy **glonassd.sh** file in **/etc/init.d** folder.<br>
Use **chmod 0755 /etc/init.d/glonassd.sh** for make it executable.<br>
Use **systemctl daemon-reload** and **update-rc.d glonassd.sh defaults** for enable autostart daemo$
Use **update-rc.d -f glonassd.sh remove** for diasble autostart without delete glonassd.sh file.<br>
Delete /etc/init.d/glonassd.sh file and use **systemctl daemon-reload** for fully cleanup daemon in$

### License
The glonassd is open-sourced software licensed under the [MIT license](http://opensource.org/licens$

### Author
This daemon is part of navigation service of [locman.org](http://locman.org/map/index.php).<br>
Scold author can be reached at <mailto:mail@locman.org>.
