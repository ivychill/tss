#!/bin/bash
#if test -d bin; then
#rm -rf bin
#fi
tar -xzvf tss.tar.gz
if test ! -d log; then
mkdir log
fi
#sudo cp jsoncpp-src-0.5.0/libs/linux-gcc-4.4.5/* /usr/local/lib
#sudo cp usr/local/lib/* /usr/local/lib
#echo "export LD_LIBRARY_PATH=~/jsoncpp-src-0.5.0/libs/linux-gcc-4.4.5:/usr/local/lib" >> ~/.bashrc
#echo "export TSS_HOME=$PWD" >> ~/.bashrc
#echo "export LD_LIBRARY_PATH=~/jsoncpp-src-0.5.0/libs/linux-gcc-4.4.5:/usr/local/lib" >> ~/.bash_profile
#echo "export TSS_HOME=$PWD" >> ~/.bash_profile
