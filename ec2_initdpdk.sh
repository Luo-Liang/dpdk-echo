dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "executing in dir $dir"
ifconfig
source $dir/initenv.sh
command -v aws >/dev/null 2>&1 || { echo >&2 "I require aws but it's not installed.  Aborting. Are you running on Azure?"; exit 1; }
#export RTE_SDK=/home/ubuntu/dpdk/dpdk
#export RTE_TARGET=x86_64-native-linuxapp-gcc
#export RTE_ANS=/home/ubuntu/dpdk-ans
sudo bash -c "echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
sudo bash -c "echo 2048 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages"
sudo bash -c "echo 2048 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages"
sudo mkdir /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
#nodev /mnt/huge hugetlbfs defaults 0 0
sudo modprobe uio
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/igb_uio.ko
sudo insmod $RTE_SDK/$RTE_TARGET/kmod/rte_kni.ko
#id=`curl http://169.254.169.254/latest/meta-data/instance-id`
IFNAME=`ls /sys/class/net | grep -Fxv -e lo`
pip=`hostname -i`
#if [ "$pip" == "" ]
#then
#    pip=`aws ec2 describe-instances --instance-ids $id  --query 'Reservations[].Instances[].PrivateIpAddress' --output text`
#fi

for iface in $IFNAME
do
    echo "warning: binding DPDK to $iface for interfaces with ip != $pip"
    ifaceIP=`ifconfig $iface | grep "inet addr" | cut -d ':' -f 2 | cut -d ' ' -f 1`
    if [ "$ifaceIP" == "" ]
    then
	ifaceIP=`ifdata -pa $iface`
    fi
    
    if [ "$pip" == "$ifaceIP" ]
    then
	echo "skipping $iface because it is the primary IF (primary ip = $pip, ifIP = $ifaceIP)"
	continue
    fi
    echo "bringing down $iface ($ifaceIP) for dpdk binding"
    sudo ifconfig $iface down
    pciName=`ethtool -i $iface | grep bus-info: | cut -c 11-`
    echo "pci-name: $pciName"
    sudo python $RTE_SDK/usertools/dpdk-devbind.py --bind=igb_uio $pciName
done

    
#echo "warning: binding DPDK to $IFNAME"
#sudo python $RTE_SDK/usertools/dpdk-devbind.py --bind=igb_uio $IFNAME
#sudo $RTE_ANS/ans/build/ans -c 0x2 -n 1  -- -p 0x1 --config="(0,0,1)" --enable-kni --enable-ipsync &
echo "DPDK Ok"
