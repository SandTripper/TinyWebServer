server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp ./sqlconnpool/sql_connection_pool.cpp ./sqlconnpool/sql_connection_pool.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./config/config.h ./config/config.cpp
	g++ -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp -lpthread ./sqlconnpool/sql_connection_pool.cpp ./sqlconnpool/sql_connection_pool.h ./log/log.cpp ./log/log.h ./log/block_queue.h ./config/config.h ./config/config.cpp -L/usr/lib64/mysql -lmysqlclient


clean:
	rm  -r server
