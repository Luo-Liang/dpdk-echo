source ~/plink-marcopolo/initenv.sh
dir="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd $dir/server; make;
cd $dir/client; make;
