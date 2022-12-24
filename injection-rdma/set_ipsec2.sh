LOCAL=192.168.1.20
DEV=enp1s0f0
REMOTE=192.168.1.10
SPIRL=0x28f39549
SPILR=0x622a73b4
KEYRL=0x492e8ffe718a95a00c1893ea61afc64997f4732848ccfe6ea07db483175cb18de9ae411a
KEYLR=0x093bfee2212802d626716815f862da31bcc7d9c44cfe3ab8049e7604b2feb1254869d25b

sudo ip xfrm state add src $LOCAL/24 dst $REMOTE/24 proto esp spi $SPILR reqid $SPILR mode transport aead 'rfc4106(gcm(aes))' $KEYLR 128 offload dev $DEV dir out sel src $LOCAL dst $REMOTE
sudo ip xfrm state add src $REMOTE/24 dst $LOCAL/24 proto esp spi $SPIRL reqid $SPIRL mode transport aead 'rfc4106(gcm(aes))' $KEYRL 128 offload dev $DEV dir in sel src $REMOTE dst $LOCAL
sudo ip xfrm policy add src $LOCAL dst $REMOTE dir out tmpl src $LOCAL/24 dst $REMOTE/24 proto esp reqid $SPILR mode transport
sudo ip xfrm policy add src $REMOTE dst $LOCAL dir in tmpl src $REMOTE/24 dst $LOCAL/24 proto esp reqid $SPIRL mode transport
sudo ip xfrm policy add src $REMOTE dst $LOCAL dir fwd tmpl src $REMOTE/24 dst $LOCAL/24 proto esp reqid $SPIRL mode transport

