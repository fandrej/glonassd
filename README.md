# glonassd
GPS/GLONASS tracker server for Debian

### About
**glonassd** is a linux daemon that receives data from a GPS / GLONASS trackers, processing and preserving them in a database.<br>
Written in C, Compiled with gcc (Debian 4.9.2-10) 4.9.2 for x86_64-linux-gnu.

### Tracker protocols
**Receiving:** Arnavi, Galileo (all versions), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Olympstroy), EGTS (ERA-GLONASS).<br>
**Sending (forwarding):** all receiving without reencode or reencode to Wialon NIS or EGTS.<br>
Protocols can be added using plug libraries.

### Database
PostgreSQL<br>
Databases can be added using plug libraries.

### Features
* Data transfer on the fly to another server with transcoding to a different protocol (to Wialon NIS or EGTS only) or without transcoding.
* Automatic restoration of connections to the server when the connection is broken.
* Storing data at the time of connection failure with the server and sending the stored data after the restoration of communication.
* The maximum number of relay servers: 3 for each terminal.
* Perform scheduled tasks.
* Easy configuration using .conf files
* Extensibility through plug libraries without recompilation demon.
