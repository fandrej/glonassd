# glonassd
GPS/GLONASS трекер для ОС Debian.

### Краткое описание
**glonassd** это демон для линукс, принимающий данные от GPS / GLONASS трекеров и сохраняющий их в базу данных.<br>
Демон написан на языке C, собран компилятором gcc 4.9.2 для x86_64-linux-gnu.

### Протоколы
**Принимаемые:** Arnavi, Galileo (все версии), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Олимпстрой), ЕГТС (ЭРА-ГЛОНАСС).<br>
**Передаваемые (пересылка):** все принимаемые без перекодирования или перекодирование в протокол Wialon NIS или ЕГТС.<br>
Протоколы могут быть добавлены подключением библиотек без перекомпиляции демона.

### База данных
На текущий момент используется PostgreSQL<br>
БД могут быть добавлены подключением библиотек без перекомпиляции демона.

### Возможности
* Пересылка данных трекеров на сторонние сервера с перекодированием или без него.
* Хранение данных на время обрыва связи со сторонними серверами, отправка после восстановления связи.
* Автоматическое восстановление связи со сторонними серверами в случае её потери.
* Максимальное количество ретрансляций: до 3 серверов для каждого терминала.
* Выполнение задач (скриптов) по расписанию (таймеру).
* Простая настройка двумя .conf файлами.
* Возможность добавления протоколов терминалов или баз данных подключаемыми библиотеками без перекомпиляции демона.

### Компиляция
**make all** демон + библиотека БД + библиотеки терминалов<br>
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
