#! /bin/sh
### BEGIN INIT INFO
# File:				service.sh
# Provides:         init service
# Required-Start:   $
# Required-Stop:
# Default-Start:
# Default-Stop:
# Short-Description:web service
# Author:			gao_wangsheng
# Email: 			gao_wangsheng@anyka.oa
# Date:				2012-12-27
### END INIT INFO

MODE=$1
TEST_MODE=0
WIFI_TEST=0
FACTORY_TEST=0
AGING_TEST=0
UPDATE_MODE=0
PATH=$PATH:/bin:/sbin:/usr/bin:/usr/sbin
cfgfile="/etc/jffs2/anyka_cfg.ini"

usage()
{
	echo "Usage: $0 start|stop)"
	exit 3
}

stop_service()
{
	killall -12 daemon
	echo "watch dog closed"
	sleep 5
	killall daemon
	killall cmd_serverd

	/usr/sbin/anyka_ipc.sh stop

	echo "stop network service......"
	killall net_manage.sh

    /usr/sbin/eth_manage.sh stop
    /usr/sbin/wifi_manage.sh stop
}

start_service ()
{
	ssid=`awk 'BEGIN {FS="="}/\[wireless\]/{a=1} a==1 && $1~/^ssid/{gsub(/\"/,"",$2);gsub(/\;.*/, "", $2);gsub(/^[[:blank:]]*/,"",$2);print $2}' $cfgfile`
	cmd_serverd
	if [ $WIFI_TEST = 1 ]; then
	    #insmod /usr/modules/sdio_wifi.ko
	    insmod /usr/modules/8188fu.ko
	    /mnt/wifitest/wifi_test.sh 
	    echo "start wifi test."
	elif [ $FACTORY_TEST = 1 ]; then
	    /mnt/usbnet/product_test & 
	    insmod /mnt/usbnet/otg-hs.ko
	    insmod /mnt/usbnet/usbnet.ko
	    insmod /mnt/usbnet/asix.ko
	    #insmod /mnt/usbnet/udc.ko
	    #insmod /mnt/usbnet/g_ether.ko
	    sleep 1
	    ifconfig eth0 up
	    sleep 1
	    /usr/sbin/eth_manage.sh start
	    echo "start product test."
    elif [ $AGING_TEST = 1 ]; then
	    #insmod /usr/modules/sdio_wifi.ko
	    insmod /usr/modules/8188fu.ko
	    sleep 1
	    ifconfig wlan0 up
	    sleep 1
	    /tmp/aging_test 
	    echo "start aging test."
	else
	    if [ $UPDATE_MODE = 1 ]; then
	        echo "to do software update check."
	        /usr/sbin/update_check.sh
	    fi
		#daemon
		echo 1 > /sys/class/leds/red_led/brightness
		#/usr/sbin/anyka_ipc.sh start 
		echo "start net service......"
	fi

	boot_from=`cat /proc/cmdline | grep nfsroot`
	if [ -z "$boot_from" ] && [ $FACTORY_TEST = 0 ] && [ $WIFI_TEST = 0 ] && [ $AGING_TEST = 0 ];then
		echo "start net service......"
		echo "[service.sh] find ssid = $ssid"
		export ssid
		/usr/sbin/net_manage.sh &
	else
		echo "## start from nfsroot, do not change ipaddress!"
	fi
	unset boot_from
}

restart_service ()
{
	echo "restart service......"
	stop_service
	start_service
}

#
# main:
#
#if test -e /etc/jffs2/danale.conf ;then
#	TEST_MODE=0
#else
#	TEST_MODE=1
#fi

if test -e /dev/mmcblk0p1 ;then
    mount -rw /dev/mmcblk0p1 /mnt
elif test -e /dev/mmcblk0 ;then
    mount -rw /dev/mmcblk0 /mnt
fi

if test -d /mnt/usbnet ;then
	FACTORY_TEST=1
	echo 0 > /sys/class/leds/red_led/brightness
	echo 1 > /sys/class/leds/blue_led/brightness
else
	FACTORY_TEST=0
fi

if test -d /mnt/agingtest ;then
	AGING_TEST=1
	echo 0 > /sys/class/leds/red_led/brightness
    echo 1 > /sys/class/leds/blue_led/brightness
    cp /mnt/agingtest/aging_test /tmp/
else
	AGING_TEST=0
fi

if test -d /mnt/wifitest ;then
    WIFI_TEST=1
    echo 0 > /sys/class/leds/red_led/brightness
    echo 1 > /sys/class/leds/blue_led/brightness
else
    WIFI_TEST=0
fi

if test -d /mnt/update ;then
    UPDATE_MODE=1
else
    UPDATE_MODE=0
fi

#redirect stdout and stderr to /mnt/debug/log.txt#
LOG_FILE="/mnt/debug/log.txt"
if [ -f $LOG_FILE ];then
	# Close STDOUT file descriptor
	exec 1<&-
	# Close STDERR FD
	exec 2<&-

	# Open STDOUT as $LOG_FILE file for read and write.
	exec 1<>$LOG_FILE

	# Redirect STDERR to STDOUT
	exec 2>&1
	#
else
	echo "NO DEBUG FILE"
fi

version=`env_ops -r fw_version`
if [ "$version" = "" ];then
	version=`cat /usr/local/factory_fw_version`

	echo "version=$version"
	env_ops -w fw_version -v $version
fi

case "$MODE" in
	start)
		start_service
		;;
	stop)
		stop_service
		;;
	restart)
		restart_service
		;;
	*)
		usage
		;;
esac
exit 0

