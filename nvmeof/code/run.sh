
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/xxx/spdk/dpdk/build/lib/
echo $LD_LIBRARY_PATH

echo "code requires SPDK. change the path to the lib"

#./client  -r 'trtype:PCIe traddr:0000:02:00.0'
./client  -r 'trtype:rdma adrfam:IPv4 traddr:192.168.1.21 trsvcid:4420 subnqn:nqn.2016-06.io.spdk:cnode1' 
