#include <pthread.h>
#include <time.h>
#include "errors.h"
#include <semaphore.h>

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
    int                 message_number; /* Message identifier */
    int                 cancellable; /* Either 0 or 1 */
    int                 replaced;
    time_t              time;   /* Seconds from EPOCH */
    char                message[128];
} alarm_t;

pthread_mutex_t alarm_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t alarm_cond = PTHREAD_COND_INITIALIZER;
alarm_t *alarm_list = NULL;
int current_alarm = 0;

// Used for reader/writer problem
sem_t rw_mutex, mutex;
int read_count = 0;

void print_alarm_list() {
    alarm_t *next;
    
    printf ("[list: ");
    for (next = alarm_list; next != NULL; next = next->link)
        printf ("%ld(%ld)[\"%s\"]", next->time,
            next->time - time (NULL), next->message);
    printf ("]\n");
}

alarm_t *get_alarm_at(int m_id) {
    alarm_t *next;

    for(next = alarm_list; next != NULL; next = next->link) {
        if(next->message_number == m_id)
            return next;
    }
}

int message_id_exists(int m_id) {
    alarm_t *alarm = get_alarm_at(m_id);
    
    if (alarm != NULL)
        return 1;
    else
        return 0;
    
}

void find_and_replace(alarm_t *new_alarm) {
    alarm_t *old_alarm;
    int status;
    
    sem_wait(&rw_mutex);
    
    status = pthread_mutex_lock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Lock mutex");
        
    old_alarm = get_alarm_at(new_alarm->message_number);
    old_alarm->seconds = new_alarm->seconds;
    old_alarm->time = time(NULL) + new_alarm->seconds;
    old_alarm->replaced = 1;
    strcpy(old_alarm->message , new_alarm->message);
    
    status = pthread_mutex_unlock (&alarm_mutex);
    if (status != 0)
        err_abort (status, "Unlock mutex");
        
    sem_post(&rw_mutex);
}

// TODO - Remove node from alarm list
void cancel_alarm (alarm_t *alarm) {
    
}

// Insert alarm entry on list, in order.
void alarm_insert(alarm_t *alarm) {
    int status;
    alarm_t **last, *next;

    sem_wait(&rw_mutex);
    /*
     * LOCKING PROTOCOL:
     * 
     * This routine requires that the caller have locked the
     * alarm_mutex!
     */
    last = &alarm_list;
    next = *last;
    while (next != NULL) {
        if (next->message_number >= alarm->message_number) {
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
    
    // A.3.2.1
    printf("First Alarm Request With Message Number (%d) Received at <%ld>: <%d %s>\n",
        alarm->message_number, time(NULL), alarm->seconds, alarm->message);
        
    //print_alarm_list();

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
    
    sem_post(&rw_mutex);
}

void *periodic_display_thread(void *alarm_in) {
    int alarm_replaced = 0;
    alarm_t *alarm = (alarm_t*) alarm_in;
    alarm_t *next;
    int status;
    
    // while(1) {
    //     if(alarm_list != NULL) {
    //         next = alarm_list;
            
    //         while(next->message_number != alarm->message_number)
    //             next = next->link;
                
    //         if(next == NULL || alarm->cancellable > 0) {
    //             printf("Display thread exiting at <%ld>: <%d %s>\n", 
    //                 time(NULL), alarm->seconds, alarm->message);
    //             // TODO: Terminate thread
    //             free(alarm);
    //             break;
    //         } else {
    //             if(next->replaced == 1) {
    //                 printf("Alarm With Message Number (%d) Replaced at <%ld>: <%d %s>\n",
    //                     alarm->message_number, time(NULL), alarm->seconds, alarm->message);
    //                 alarm_replaced = 1;
    //             } 
    //         }
    //     }
    // }
}

// Alarm thread - Process new alarms
void *alarm_thread(void *arg) {
    pthread_t display_t;
    alarm_t *alarm;
    int status;

    while(1) {
        if (alarm_list != NULL) {
            alarm = alarm_list;
            while(alarm->message_number != current_alarm){
                alarm = alarm->link;
            }
        
            if(alarm->cancellable == 0){
                status = pthread_create(&display_t, NULL, periodic_display_thread, (void *)alarm);
                if(status != 0)
                    err_abort(status, "Create periodic display thread");
            } else {
                // TODO: remove the alarm from the list
            }
            printf("Alarm Request With Message Number (%d) Processed at <%ld>: <%d %s>\n", 
                alarm->message_number, time(NULL), alarm->seconds, alarm->message);
            status = pthread_cond_wait (&alarm_cond, &alarm_mutex);
        }
    }

}

// main thread
int main (int argc, char *argv[]) {
    int status;
    int cancel_message_id = 0;
    char line[256];
    alarm_t *alarm;
    pthread_t thread;
    
    //semaphore init
    sem_init(&mutex, 0, 1);
    sem_init(&rw_mutex, 0, 1);
    
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
                alarm->cancellable = 0;
                alarm->replaced = 0;
                current_alarm = alarm->message_number;
                /*
                 * Insert the new alarm into the list of alarms,
                 * sorted by message_number.
                 */
                alarm_insert (alarm);
                pthread_cond_signal(&alarm_cond);
                
                status = pthread_mutex_unlock (&alarm_mutex);
                if (status != 0)
                    err_abort (status, "Unlock mutex");
            } else {
                find_and_replace(alarm);
                //print_alarm_list();
                // A3.2.2 Print Statement
                printf("Replacement Alarm Request With Message Number (%d) Received at <%ld>: <%d %s>\n", 
                        alarm->message_number, time(NULL), alarm->seconds, alarm->message);
            }
            
        } else if(cancel_command_parse == 1)  {
            // TODO 3.2.3 -- Check if the message id exists
            if(message_id_exists(cancel_message_id) == 0) {
                printf("Error: No Alarm Request With Message Number (%d) to Cancel!\n", cancel_message_id);
            } else{
                alarm_t *at_alarm = get_alarm_at(cancel_message_id);
                if (at_alarm->cancellable > 0)
                    printf("Error: More Than One Request to Cancel Alarm Request With Message Number (%d)!\n", cancel_message_id);
                else {
                    at_alarm->cancellable = at_alarm->cancellable + 1;
                    pthread_cond_signal(&alarm_cond);
                    printf("Cancel Alarm Request With Message Number (%d) Received at <%ld>: <%d %s>\n", 
                        at_alarm->message_number, time(NULL), at_alarm->seconds, at_alarm->message);
                }
            }
        } else {
            fprintf (stderr, "Bad command\n");
            free (alarm);
        }
    }
}
