#!/bin/sh
### BEGIN INIT INFO
# Provides:          glonassd
# Required-Start:    $all
# Required-Stop:     $local_fs $remote_fs $network $syslog $postgresql
# Should-Start:      $postgresql
# Should-Stop:       $postgresql
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Start glonassd
# Description:       Start glonassd
### END INIT INFO

# after change this file run: systemctl daemon-reload

# if run error: 
# /bin/sh^M: плохой интерпретатор: Нет такого файла или каталога
# (/bin/sh^M: bad interpreter: No such file or directory)
# run: sed -i 's/\r//' /etc/init.d/glonassd.sh

# see also:
# https://www.itroad.ru/startovye-skripty-v-debian-avtozagruzka
# http://doc.ubuntu-fr.org/tutoriel/comment_transformer_un_programme_en_service
# http://man7.org/linux/man-pages/man8/start-stop-daemon.8.html

DAEMON="/opt/glonassd/glonassd"
daemon_OPT="-c /opt/glonassd/glonassd.conf"
daemon_NAME="glonassd"

PATH="/sbin:/bin:/usr/sbin:/usr/bin:/opt/glonassd"
HOME="/opt/glonassd"

test -x $DAEMON || exit 0

. /lib/lsb/init-functions

case "$1" in

	start)
		$DAEMON start $daemon_OPT
		;;

	stop)
		$DAEMON stop
		;;

	restart)
		$DAEMON restart $daemon_OPT
		;;

	force-restart)
		$DAEMON stop
		$DAEMON start $daemon_OPT
		;;

	force-stop)
		$DAEMON stop
		killall -q $daemon_NAME || true
		sleep 2
		killall -q -9 $daemon_NAME || true
		;;

	status)
		status_of_proc "$daemon_NAME" "$DAEMON" "system-wide $daemon_NAME" && exit 0 || exit $?
		;;

	*)
		echo "Usage: /etc/init.d/$daemon_NAME {start|stop|force-stop|restart|force-restart|status}"
		exit 1
		;;
esac
exit 0
