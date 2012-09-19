#!/bin/bash
> test_client.sh
i=0
while(($i<160))
do
  ((i++))
  echo "bin/test_client hello$i&" >> test_client.sh
done
chmod +x test_client.sh

> test_local.sh
i=0
while(($i<64))
do
  ((i++))
  echo "bin/test_client hello$i&" >> test_local.sh
done
chmod +x test_local.sh

> test_remote.sh
i=0
while(($i<160))
do
  ((i++))
  echo "bin/test_remote hello$i&" >> test_remote.sh
done
chmod +x test_remote.sh
