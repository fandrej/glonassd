# glonassd
GPS/GLONASS трекер для ОС Debian.

### Краткое описание
**glonassd** это демон для линукс, принимающий данные от GPS / GLONASS трекеров и сохраняющий их в базу данных.<br>
Демон написан на языке C, собран компилятором gcc 4.9.2 для x86_64-linux-gnu.

### Протоколы
**Принимаемые:** Arnavi, Galileo (все версии), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Олимпстрой), ЕГТС (ЭРА-ГЛОНАСС).<br>
**Передаваемые (пересылка):** все принимаемые без перекодирования или перекодирование в протокол Wialon NIS или ЕГТС.<br>
Протоколы реализованы могут быть добавлены подключением библиотек без перекомпиляции демона.

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
**make all** компиляция демона + библиотеки БД + библиотеки терминалов<br>
**make glonassd** компиляция только демона без библиотек<br>
**make pg** компиляция библиотеки базы данных (PostgreSQL)<br>
**make name** компиляция библиотеки протокола терминала **name**

### Установка
Создать папку для демона и скопировать в неё файлы **glonassd, *.so, *.sql**, или использовать папку сборки демона.<br>
В базе данных PostgreSQL создать таблицу "tgpsdata" (см. скрипт tgpsdata.sql).<br>
Если работает фаерволл, разрешить входящие подключения на портах демона (см. файл glonassd.conf).

### Настройка
В файле **glonassd.conf** в секции **server** изменить значения параметров:<br>
**listen** - IP адрес входящих подключений<br>
**transmit** - IP адрес с которого производится ретрансляция<br>
**log_file** - полный путь к лог-файлу<br>
**db_host, db_port, db_name, db_schema, db_user, db_pass** - параметры подключения к БД PostgreSQL<br>
Закомментировать или раскомментировать секции протоколов терминалов в зависимости от используемых и указать порты для подключения терминалов.

Для включения ретрансляции терминалов изучить комментарии в секции **forward** в файле **glonassd.conf**.<br>
Для включения задач по расписанию изучить комментарии к параметру **timer** в секции **server** в файле **glonassd.conf**.

### Запуск
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
