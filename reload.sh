rmmod vmxnet3
rmmod ixgbe
rmmod ixgbevf
rmmod netmap

insmod /home/user/proj/netmap/netmap.ko
insmod /home/user/proj/netmap/ixgbevf-4.3.2/src/ixgbevf.ko
insmod /home/user/proj/netmap/vmxnet3/vmxnet3.ko
