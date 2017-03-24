#include <pthread.h>
#include <time.h>
#include "errors.h"

// Reqest type constants
#define typeA 'A'
#define typeB 'B'

/*
 * The "alarm" structure now contains the time_t (time since the
 * Epoch, in seconds) for each alarm, so that they can be
 * sorted. Storing the requested number of seconds would not be
 * enough, since the "alarm thread" cannot tell how long it has
 * been on the list.
 */
typedef struct alarm_tag {
    struct alarm_tag    *link;
    int                 seconds;
    time_t              time;   /* Seconds from EPOCH */
    int                 message_number; /* Message identifier */
    char                request_type; /* Either A or B*/    
    char                message[128];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
time_t current_alarm = 0;

// TODO
void cancel_alarm () {
    
}

/*
 * Insert alarm entry on list, in order.
 */
void alarm_insert (alarm_t *alarm)
{
    int status;
    alarm_t **last, *next;

    /*
     * LOCKING PROTOCOL:
     * 
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while (next != NULL) {
        if (next->time >= alarm->time) {
            alarm->link = next;
            *last = alarm;
            break;
        }
        last = &next->link;
        next = next->link;
    }
    /*
     * If we reached the end of the list, insert the new alarm
     * there.  ("next" is NULL, and "last" points to the link
     * field of the last item, or to the list header.)
     */
    if (next == NULL) {
        *last = alarm;
        alarm->link = NULL;
    }

    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%d(%d)[\"%s\"] - %c ", next->time,
            next->time - time (NULL), next->message, next->request_type);
    printf ("]\n");

    /*
     * Wake the alarm thread if it is not busy (that is, if
     * current_alarm is 0, signifying that it's waiting for
     * work), or if the new alarm comes before the one on
     * which the alarm thread is waiting.
     */
    if (current_alarm == 0 || alarm->time < current_alarm) {
        current_alarm = alarm->time;
        status = pthread_cond_signal (&alarm_cond);
        if (status != 0)
            err_abort (status, "Signal cond");
    }
}

/*
 * The alarm thread's start routine.
 */
void *alarm_thread (void *arg) {
    alarm_t *alarm;
    struct timespec cond_time;
    time_t now;
    int status, expired;

    /*
     * Loop forever, processing commands. The alarm thread will
     * be disintegrated when the process exits. Lock the mutex
     * at the start -- it will be unlocked during condition
     * waits, so the main thread can insert alarms.
     */
    status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");
    while (1) {
        /*
         * If the alarm list is empty, wait until an alarm is
         * added. Setting current_alarm to 0 informs the insert
         * routine that the thread is not busy.
         */
        current_alarm = 0;
        while (alarm_list == NULL) {
            status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
            if (status != 0)
                err_abort (status, "Wait on cond");
            }
        alarm = alarm_list;
        alarm_list = alarm->link;
        now = time (NULL);
        expired = 0;
        if (alarm->time > now) {
#ifdef DEBUG
            printf ("[waiting: %d(%d)\"%s\"]\n", alarm->time,
                alarm->time - time (NULL), alarm->message);
#endif
            cond_time.tv_sec = alarm->time;
            cond_time.tv_nsec = 0;
            current_alarm = alarm->time;
            while (current_alarm == alarm->time) {
                status = pthread_cond_timedwait (
                    &alarm_cond, &alarm_mutex, &cond_time);
                if (status == ETIMEDOUT) {
                    expired = 1;
                    break;
                }
                if (status != 0)
                    err_abort (status, "Cond timedwait");
            }
            if (!expired)
                alarm_insert (alarm);
        } else
            expired = 1;
        if (expired) {
            printf ("(%d) %s\n", alarm->seconds, alarm->message);
            free (alarm);
        }
    }
}

int message_id_exists(int m_id) {
    alarm_t *next;

    for(next = alarm_list; next != NULL; next = next->link) {
        if(next->message_number == m_id)
            return 1;
        else
            return 0;
    }
}

int main (int argc, char *argv[]) {
    int status;
    int cancel_message_id = 0;
    char line[256];
    alarm_t *alarm;
    pthread_t thread;

    status = pthread_create (&thread, NULL, alarm_thread, NULL);
    if (status != 0)
        err_abort (status, "Create alarm thread");
    while (1) {
        printf ("Alarm> ");
        if (fgets (line, sizeof (line), stdin) == NULL) exit (0);
        if (strlen (line) <= 1) continue;
        alarm = (alarm_t*)malloc (sizeof (alarm_t));
        
        if (alarm == NULL)
            errno_abort ("Allocate alarm");

        /*
         * Parse input line into seconds (%d) and a message
         * (%64[^\n]), consisting of up to 64 characters
         * separated from the seconds by whitespace.
         */
         
        int insert_command_parse = sscanf(line, "%d Message(%d) %128[^\n]",
            &alarm->seconds, &alarm->message_number, alarm->message);
            
        int cancel_command_parse = sscanf(line, "Cancel: Message(%d)", &cancel_message_id);
        
        if(insert_command_parse == 3 && alarm->seconds > 0 && alarm->message_number > 0) {
            // Check if the message_number exits in the alarm list
            if(message_id_exists(alarm->message_number) == 0) {
                status = pthread_mutex_lock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Lock mutex");
                alarm->time = time (NULL) + alarm->seconds;
                alarm->request_type = typeA;
                /*
                 * Insert the new alarm into the list of alarms,
                 * sorted by expiration time.
                 */
                alarm_insert (alarm);
                
                // A3.2.1 Print Statement
                printf("First Alarm Request With Message Number (%d) Received at <%ld>: <%d %s>\n", 
                        alarm->message_number, time(NULL), alarm->seconds, alarm->message);
                        
                status = pthread_mutex_unlock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
            } else {
                // A3.2.2 Print Statement
                printf("Replacement Alarm Request With Message Number (%d) Received at <%ld>: <%d %s>\n", 
                        alarm->message_number, time(NULL), alarm->seconds, alarm->message);
                // TODO A3.2.2
            }
            
        } else if(cancel_command_parse == 1)  {
            // TODO
            printf("TODO: Cancel Message\n");
            // TODO 3.2.3 -- Check if the message id message id message id exists
            printf("Error: No Alarm Request With Message Number (%d) to Cancel!", cancel_message_id);
        } else {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        }
         
    } // While closing brace
}
