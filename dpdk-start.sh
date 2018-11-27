echo "Remove kernel NIC drivers and install DPDK drivers"
read -p "Ok (Y or N): " ok
if [ $ok != 'y' ] && [ $ok != 'Y' ] 
then
    echo "Do not do"
    exit 1
fi 
echo "Setup huge pages"
echo 512 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
echo "There are `cat /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages` 2MB hugepages"
echo 1 > /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages
echo "There are `cat /sys/kernel/mm/hugepages/hugepages-1048576kB/nr_hugepages` 1GB hugepages"
#echo "Remove any prior user mode drivers"
#rmmod rte_kni
#rmmod igb_uio
#rmmod uio
echo "Install user mode drivers"
modprobe uio

insmod $(dirname "$0")/dpdk/x86_64-native-linuxapp-gcc/kmod/igb_uio.ko
#insmod /data/f-stack/dpdk/x86_64-native-linuxapp-gcc/kmod/rte_kni.ko
#python dpdk/usertools/dpdk-devbind.py --status
echo "Bind to default ethernet driver"
ifconfig eth0 down
python $(dirname "$0")/dpdk/usertools/dpdk-devbind.py --force --bind=igb_uio eth0 # assuming that use 10GE NIC and eth0
ulimit -n 131072
