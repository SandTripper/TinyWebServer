server: main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp
	g++ -o server main.cpp ./threadpool/threadpool.h ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./timer/time_wheel_timer.h ./timer/time_wheel_timer.cpp -lpthread 


clean:
	rm  -r server
