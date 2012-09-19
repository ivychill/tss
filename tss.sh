#!/bin/bash
#
RETVAL=0;

do_start() {
echo "Starting tss"
su - chenfeng -c "cd tss && ./run.sh"
}

do_stop() {
echo "Stopping tss"
killall traffic_apns
killall traffic_feed
killall traffic_forward
killall traffic_router
}

do_restart() {
do_stop
do_start
}

case "$1" in
start)
  do_start
;;
stop)
  do_stop
;;
restart)
  do_restart
;;
*)
echo $"Usage: $0 {start|stop|restart}"
exit 1
esac

exit $RETVAL
