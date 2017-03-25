# Build an executable named New_Alarm_Cond from New_Alarm_Cond.c

all: New_Alarm_Cond.c
	cc New_Alarm_Cond.c -o New_Alarm_Cond -D_POSIX_PTHREAD_SEMANTICS -lpthread
