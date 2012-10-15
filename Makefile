#started with '#' are comments
JSON_HOME=/home/chenfeng/jsoncpp-src-0.5.0
#INC=-I$(JSON_HOME)/include/json
LIB=-L$(JSON_HOME)/libs/linux-gcc-4.6 -lzmq -llog4cplus -lprotobuf -ljson_linux-gcc-4.6_libmt
LIB_ADD = -lmongoclient -lboost_thread -lboost_system -lboost_filesystem -lboost_program_options

BIN_PATH=bin
PRG=traffic_router traffic_forward traffic_feed traffic_apns test_probe test_client test_remote
OBJ=*.o

CC=g++
CC_FLAG=-Wall
#CC_FLAG=-Wall -D_SANDBOX
#CC_FLAG=-Wall -D_SANDBOX -include gcc-preinclude.h

#/libjson_linux-gcc-4.2.1_libmt.a

all: traffic_router traffic_forward traffic_feed traffic_apns test_probe test_client test_remote 

.cpp.o:
	@echo "Compile $(OBJ) begin......"
	$(CC) $(CC_FLAG) -c $<
	@echo "Compile $(OBJ) end......"

.cc.o:
	@echo "Compile $(OBJ) begin......"
	$(CC) $(CC_FLAG) -c $<
	@echo "Compile $(OBJ) end......"

traffic_router: traffic_router.o tss_log.o
	@echo "Link traffic_router begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB)
	@echo "Link traffic_router end......"

traffic_feed: traffic_feed_main.o traffic_feed_be.o traffic_feed_fe.o tss.pb.o tss_log.o tss_helper.o traffic_feed.h
	@echo "Link traffic_feed begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB) $(LIB_ADD)
	@echo "Link traffic_feed end......"

traffic_apns: traffic_apns.o tss_helper.o tss_log.o traffic_apns.h
	@echo "Link traffic_apns begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB) $(LIB_ADD) -lssl
	@echo "Link traffic_apns end......"

traffic_forward: traffic_forward.o
	@echo "Link traffic_forward begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB)
	@echo "Link traffic_forward end......"

test_probe: test_probe.o
	@echo "Link test_probe begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB)
	@echo "Link test_probe end......"

test_client: test_client.o tss.pb.o tss_log.o tss_helper.o
	@echo "Link test_client begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB) $(LIB_ADD)
	@echo "Link test_client end......"

test_remote: test_remote.o tss.pb.o tss_log.o tss_helper.o
	@echo "Link test_remote begin......"
	$(CC) $(CC_FLAG) -o $(BIN_PATH)/$@ $^ $(LIB) $(LIB_ADD)
	@echo "Link test_remote end......"

clean:
	@echo "Removing linked and compiled files......"
	rm -f $(OBJ) tss.tar.gz
	cd $(BIN_PATH) && rm -f $(PRG)
	cd log && rm -f *.log*

.phony: clean
