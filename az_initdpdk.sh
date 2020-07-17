dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
echo "executing in dir $dir"
source $dir/initenv.sh
#command -v az >/dev/null 2>&1 || { echo >&2 "I require az but it's not installed.  Aborting. Are you running on AWS EC2?"; exit 1; }
#export RTE_SDK=/home/ubuntu/dpdk/dpdk
#export RTE_TARGET=x86_64-native-linuxapp-gcc
#export RTE_ANS=/home/ubuntu/dpdk-ans
sudo bash -c "echo 2048 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages"
sudo bash -c "echo 2048 > /sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages"
sudo bash -c "echo 2048 > /sys/devices/system/node/node1/hugepages/hugepages-2048kB/nr_hugepages"
echo "mounting huge pages..."
sudo mkdir /mnt/huge
sudo mount -t hugetlbfs nodev /mnt/huge
#nodev /mnt/huge hugetlbfs defaults 0 0
echo "init ibuverbs..."
sudo modprobe -a ib_uverbs
#id=`curl http://169.254.169.254/latest/meta-data/instance-id`
echo "DPDK Ok"
echo "\n"
