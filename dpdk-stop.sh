echo "Remove DPDK NIC drivers and install kernel drivers"
read -p "Ok (Y or N): " ok
if [ $ok != 'y' ] && [ $ok != 'Y' ]
then
    echo "Do not do"
    exit 1
fi
$(dirname "$0")/dpdk/usertools/dpdk-devbind.py -b vmxnet3 03:00.0
