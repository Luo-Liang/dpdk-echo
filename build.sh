source ~/plink-marcopolo/initenv.sh
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LIBRARY_PATH=/home/ubuntu/softiwarp-user-for-linux-rdma/build/lib:/usr/local/cuda/lib64:/usr/local/lib
#cd $dir/server; make clean; make;
cd $dir/client; make clean; make;
