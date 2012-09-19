rm log/test*.log
cp sample.json.shennan sample.json
bin/test_probe&
sleep 1
bin/test_client&
sleep 1
cp sample.json.origin sample.json
bin/test_probe&
sleep 1
bin/test_client&
