server: main.cpp ./threadpool/threadpool.h \
		./http/http_conn.cpp ./http/http_conn.h \
		./http/session_manager.h ./http/session_manager.cpp\
		./http/md5.h\
		./locker/locker.h \
		./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp \
		./mysqlconnpool/mysql_connection_pool.cpp ./mysqlconnpool/mysql_connection_pool.h \
		./redisconnpool/redis_connection_pool.cpp ./redisconnpool/redis_connection_pool.h \
		./log/log.cpp ./log/log.h ./log/block_queue.h \
		./config/config.h ./config/config.cpp
	g++ -o server -std=c++11 main.cpp \
		./threadpool/threadpool.h \
		./http/http_conn.cpp ./http/http_conn.h \
		./http/session_manager.h ./http/session_manager.cpp\
		./http/md5.h\
		./locker/locker.h \
		./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp \
		./mysqlconnpool/mysql_connection_pool.cpp ./mysqlconnpool/mysql_connection_pool.h \
		./redisconnpool/redis_connection_pool.cpp ./redisconnpool/redis_connection_pool.h \
		./log/log.cpp ./log/log.h ./log/block_queue.h \
		./config/config.h ./config/config.cpp \
		-L/usr/lib64/mysql -lmysqlclient \
		-lpthread \
		-L/usr/local/lib/ -lhiredis
		

clean:
	rm  -r server
