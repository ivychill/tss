#!/bin/bash
#tar -czvf tss.tar.gz README *.sh bin etc ../jsoncpp-src-0.5.0/libs/linux-gcc-4.6/libjson_linux-gcc-4.6_libmt.so /usr/local/lib/libprotobuf.so* /usr/local/lib/libzmq.so*
#make
tar -czvf tss.tar.gz README *.sh bin etc tss.json*
scp tss.tar.gz chenfeng@roadcloud:~/tss
#scp install.sh chenfeng@roadcloud:~/tss
#scp tss.tar.gz tss@roadcloud:~
#scp install.sh tss@roadcloud:~
