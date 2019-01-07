source ~/plink-marcopolo/initenv.sh
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
export LIBRARY_PATH=$LD_LIBRARY_PATH
cd $dir/server; make clean; make;
cd $dir/client; make clean; make;
