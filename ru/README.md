# glonassd
Сервер для GPS/GLONASS трекеров, работающий в операционной системе Debian.

### Краткое описание
**glonassd** это линукс-демон, принимающий данные от GPS / GLONASS трекеров и сохраняющий их в базу данных.<br>
Демон написан на языке C, собран компилятором gcc 6.3.0 для x86_64-linux-gnu (64-битная версия).

### Протоколы
**Принимаемые:** Arnavi-4/5, Galileo (все версии), GPS101-GPS103, SAT-LITE / SAT-LITE2, Wialon IPS, Wialon NIS (SOAP / Олимпстрой), ЕГТС (ЭРА-ГЛОНАСС), TQ GPRS (H02).<br>
**Передаваемые (пересылка):** все принимаемые без перекодирования или перекодирование в протоколы EGTS, Wialon NIS, WialonIPS, Galileo.<br>
Протоколы реализованы и могут быть добавлены подключением библиотек без перекомпиляции демона.

### База данных
PostgreSQL<br>
Redis<br>
Oracle<br>
БД могут быть добавлены подключением библиотек без перекомпиляции демона.

### Возможности
* Пересылка данных трекеров на сторонние сервера с перекодированием или без него.
* Хранение данных на время обрыва связи со сторонними серверами, отправка после восстановления связи.
* Автоматическое восстановление связи со сторонними серверами в случае её потери.
* Максимальное количество ретрансляций: до 3 серверов для каждого терминала.
* Выполнение задач (скриптов) по расписанию (таймеру), максимально 5 таймеров.
* Простая настройка двумя .conf файлами. (Примеры: [glonassd.conf](https://github.com/fandrej/glonassd/wiki/glonassd.conf), [forward.conf](https://github.com/fandrej/glonassd/wiki/forward.conf))
* Возможность добавления протоколов терминалов или баз данных подключаемыми библиотеками без перекомпиляции демона.
* Запуск в режиме демона или простого приложения


### Установка
#### Подготовка
```
apt install build-essential libpq-dev
```

#### Git версия
Предполагается установка в папку /opt/glonassd
```
cd /opt
git clone https://github.com/fandrej/glonassd.git
cd glonassd
make min
```

### Компиляция
**make all** компиляция демона + библиотек БД (Postgresql + Redis + Oracle) + библиотек терминалов<br>
**make min** компиляция демона + библиотеки БД Postgresql + библиотек терминалов<br>
**make glonassd** компиляция только демона без библиотек<br>
**make pg** компиляция библиотеки базы данных (PostgreSQL)<br>
**make name** компиляция библиотеки протокола терминала **name**<br>

[Дополнительная информация о сторонних библиотеках](https://github.com/fandrej/glonassd/wiki/Compilation)


### Настройка
Создать папки forward & logs: `mkdir -p forward logs`.<br>
В файле **glonassd.conf** в секции **server** изменить значения параметров:<br>
**listen** - IP адрес входящих подключений<br>
**transmit** - IP адрес с которого производится ретрансляция<br>
**log_file** - полный путь к лог-файлу<br>
**db_host, db_port, db_name, db_schema, db_user, db_pass** - параметры подключения к БД PostgreSQL<br>
Закомментировать или раскомментировать секции протоколов терминалов в зависимости от используемых и указать порты для подключения терминалов.

Для включения ретрансляции терминалов изучить комментарии в секции **forward** в файле **glonassd.conf**.<br>
Для включения задач по расписанию изучить комментарии к параметру **timer** в секции **server** в файле **glonassd.conf**.

### Проверьте ограничения размера очереди сообщений POSIX
Проверьте ограничение длины очереди сообщений POSIX с помощью `ulimit -q` или `ulimit -a` и посмотрите значение "POSIX message queue".
По умолчанию оно равно 819200 байтам. Увеличьте это значение как минимум до 81920000.
Для этого добавьте в файл /etc/security/limits.conf следующие строки:
```
root    soft    msgqueue        81920000
root    hard    msgqueue        81920000
*       soft    msgqueue        81920000
*       hard    msgqueue        81920000
```
и перезагрузите систему.
Если в процессе работы в лог-файле появится сообщение "mq_send(config->db_queue) message queue is already full", то размер очереди надо увеличить ещё.

### Запуск
Командой **sudo ./glonassd start** из папки с демоном, в консольном режиме, CTRL+C остановки.<br>
Параметр -d используется для запуска в режиме демона.<br>
Параметр -c path/to/config/file используется для указания файла настроек, находящегося в другой папке.<br>
Если демон уже настроен для автоматического старта, команды **service glonassd start** и **service glonassd stop** запускают и останавливают сервис.

### Автозапуск при старте системы
#### Как демон
Отредактировать значение переменной **DAEMON** в файле **glonassd.sh**, указав полный путь к файлу **glonassd**.<br>
Cкопировать файл **glonassd.sh** в папку **/etc/init.d**.<br>
Сделать этот файл исполняемым командой **chmod 0755 /etc/init.d/glonassd.sh**.<br>
Командами **systemctl daemon-reload** и **update-rc.d glonassd.sh defaults** разрешить автоматический запуск демона.<br>
Командой **update-rc.d -f glonassd.sh remove** запретить автоматический запуск демона без удаления файла glonassd.sh.<br>
Удалить файл /etc/init.d/glonassd.sh и командой **systemctl daemon-reload** очистить информацию о демоне в системе.
#### Через supervisor
```
sudo apt install supervisor
sudo ln -s $(pwd)/glonassd.supervisor.conf /etc/supervisor/conf.d/glonassd.supervisor.conf
sudo supervisorctl reread
sudo supervisorctl update glonassd
sudo supervisorctl status
```

### Лицензия
glonassd это открытое ПО под лицензией [MIT](http://licenseit.ru/wiki/index.php/MIT_License).

### Документация и API
[Документация](https://github.com/fandrej/glonassd/wiki) в процессе написания.

### Автор
Федоров Андрей, Курган, Россия.<br>
<mailto:mail@locman.org>

### Эпилог
Демон glonassd это часть навигационного сервиса [locman.org](http://locman.org/map/index.php).

### Исправления
12.06.2020<br>
* Исправлено получение больших данных (с других серверов)
* Исправлен протокол EGTS
* Улучшено использование процессора
* Добавлена база данных Redis

01.05.2021<br>
* Добавлен протокол TQ GPRS (H02) - SinoTrack ST-901
* Добавлен протокол ретранслятора WialonIPS
* Добавлен протокол ретранслятора Galileo
* Небольшие исправления

13.11.2021<br>
* Добавлена библиотека для работы с базой данных Oracle
* Добавлены логи в библиотеках баз данных

04.04.2022<br>
* Добавлена обработка сообщений CT_MSGTO, CT_MSGFROM EGTS_COMMANDS_SERVICE протокола ЕГТС

17.04.2022<br>
* Добавлен протокол GoSafe (устройства Gosafe G*/Proma Sat G*)
