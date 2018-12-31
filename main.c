#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/queue.h>
#include <sys/select.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

struct timer {
    uint32_t id;
    int32_t count;
    char    name[256];
    char    command[256];
    struct timeval interval;
    struct timeval expired;
    LIST_ENTRY(timer) entry;
};

enum timer_ctl_action_e {
    TIMER_CTL_ACT_NONE,
    TIMER_CTL_ACT_ADD,
    TIMER_CTL_ACT_MOD,
    TIMER_CTL_ACT_DUMP,
    TIMER_CTL_ACT_DEL,
};

struct timer_ctl {
    struct timer timer;
    enum timer_ctl_action_e action;
};

struct timer_list {
    size_t nextid;
    size_t count;
    struct timer *lh_first;
    struct timeval res;
};

#if 0
static inline void curr_clock(struct timeval* tv)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec/1000;
}
#endif

static struct timer* find_minimal(struct timer_list* list)
{
    struct timer* ptr = NULL;
    struct timer* minimal = LIST_FIRST(list);

    LIST_FOREACH(ptr, list, entry){
        if(timercmp(&minimal->expired, &ptr->expired, >)){
            minimal = ptr;
        }
    }
    
    return minimal;
}

static struct timer* find_timer(struct timer_list* list, struct timer* timer)
{
    struct timer* ptr;

    LIST_FOREACH(ptr, list, entry){
        if(timer->id == ptr->id || !strncmp(timer->name, ptr->name, strlen(ptr->name))){
            break;
        }
    }

    return ptr;
}

static int add_timer(struct timer_list* list, struct timer* timer)
{
    if(timer->interval.tv_sec < 0 || timer->interval.tv_sec < 0){
        return -EINVAL;
    }else if(timer->interval.tv_sec == 0 && timer->interval.tv_sec == 0){
        return -EINVAL;
    }

    timer->id = list->nextid;
    
    if(find_timer(list, timer)){
        return -EEXIST;
    }

    struct timer* ptr = (struct timer*)malloc(sizeof(struct timer));
    if(!ptr){
        return -ENOMEM;
    }
    
    memcpy(ptr, timer, sizeof(*timer));
    ptr->id = list->nextid++;
    ptr->expired = ptr->interval;
    LIST_INSERT_HEAD(list, ptr, entry);
    
    return 0;
}

static void exec_timer_cb(struct timer* timer)
{
    int pid = fork();
    if(0 == pid){
        daemon(0, 1);
        printf("execute: %s\n", timer->command);
        exit(system(timer->command));
    }else if(0 < pid){
        waitpid(pid, NULL, 0);
    }else{
        printf("failed to execute command\n");
    }
}

static void delete_timer(struct timer_list* list, struct timer* timer)
{
    LIST_REMOVE(timer, entry);
    free(timer);
}


static void update_timer_list(struct timer_list* list, struct timeval* tv)
{
    struct timeval result;
    struct timer* ptr = NULL;
    struct timer* deleted = NULL;

    LIST_FOREACH(ptr, list, entry){
        if(deleted){
            delete_timer(list, deleted);
            deleted = NULL;
        }
        timersub(&ptr->expired, tv, &result);
        if(result.tv_sec <= 0 && result.tv_usec <= 0){
            exec_timer_cb(ptr);
            if(ptr->count >= 1){
                ptr->count --;
                if(ptr->count == 0){
                    deleted = ptr;
                }
            }
            ptr->expired = ptr->interval;
        }else{
            ptr->expired = result;
        }
    }
    if(deleted){
        delete_timer(list, deleted);
        deleted = NULL;
    }
}


static void update_timer(struct timer_list* list, struct timer* timer)
{
    if(timer->interval.tv_sec < 0 || timer->interval.tv_usec < 0){
        printf("invalid interval\n");
        return;
    }

    struct timer* ptr = find_timer(list, timer);
    if(!ptr){
        printf("find timer failed\n");
        return;
    }
    
    printf("%ld, %ld\n", timer->interval.tv_sec, timer->interval.tv_usec);
    if(strlen(timer->command) > 0)
        memcpy(ptr->command, timer->command, sizeof(ptr->command));
    if(timer->interval.tv_sec > 0 || timer->interval.tv_usec > 0 ){
        printf("update timer\n");
        memcpy(&ptr->interval, &timer->interval, sizeof(ptr->interval));
        ptr->expired = ptr->interval;
    }
}

static void dump_timer_list(struct timer_list* list)
{
    struct timer* ptr;

    LIST_FOREACH(ptr, list, entry){
        printf("timer(%s) id(%u)\n"
                "\tcount      : %u\n"
                "\tinterval   : secs: %ld, usecs: %ld\n"
                "\texpired    : secs: %ld, usecs: %ld\n" 
                "\tcommmand   : %s\n\n",
                ptr->name, ptr->id, 
                ptr->count ? ptr->count : ~0U,
                ptr->interval.tv_sec,
                ptr->interval.tv_usec,
                ptr->expired.tv_sec,
                ptr->expired.tv_usec,
                ptr->command);
    }
}

int timerd_main(int argc, char* argv[])
{
    struct timer_list timer_list;

    memset(&timer_list, 0, sizeof(struct timer_list));
    timer_list.res.tv_usec = 1;

    unlink("/tmp/timerd.fifo");
    if(0 > mkfifo("/tmp/timerd.fifo", 0666)){
        perror("mkfifo");
        return -1;
    }
    int fd = open("/tmp/timerd.fifo", O_RDWR|O_NONBLOCK);
    if(fd < 0){
        perror("open fifo file\n");
        return -1;
    }
    
    int maxfd = fd;
    fd_set rfdset;
    fd_set rfdset_save;
    FD_ZERO(&rfdset_save);
    FD_SET(fd, &rfdset_save);

    struct timeval selecting_time;
    struct timer* timeout = NULL;
    bool running = true;
    while(running){
        rfdset = rfdset_save;
        if(timeout){
           selecting_time = timeout->expired; 
        }
        struct timeval* val = (timeout ? &selecting_time : NULL);
        int ret = select(maxfd+1, &rfdset, NULL, NULL, val);
        if(ret < 0){
            if(errno != EINTR){
                perror("select");
                break;
            }
            continue;
        }else if(ret > 0){
            if(timeout){
                struct timeval tmp;
                timersub(&timeout->expired, &selecting_time, &tmp);
                update_timer_list(&timer_list, &tmp);
                timeout = NULL;
            }

            struct timer_ctl timer_ctl;
            int retlen = read(fd, &timer_ctl, sizeof(timer_ctl));
            if(retlen != sizeof(timer_ctl)){
                printf("error1\n");
                timeout = find_minimal(&timer_list);
                continue;
            }

            if(timer_ctl.action == TIMER_CTL_ACT_ADD){
                add_timer(&timer_list, &timer_ctl.timer);
            }else if(timer_ctl.action == TIMER_CTL_ACT_MOD){
                printf("update timer: %d\n", retlen);
                update_timer(&timer_list, &timer_ctl.timer);
            }else if(timer_ctl.action == TIMER_CTL_ACT_DEL){
                struct timer* ptr = find_timer(&timer_list, &timer_ctl.timer);
                if(ptr){
                    delete_timer(&timer_list, ptr);
                }
            }else if(timer_ctl.action == TIMER_CTL_ACT_DUMP){
                dump_timer_list(&timer_list);
            }

            timeout = find_minimal(&timer_list);
            continue;
        }else{
            if(timeout){
                struct timeval tmp;
                timersub(&timeout->expired, &selecting_time, &tmp);
                update_timer_list(&timer_list, &tmp);
                timeout = find_minimal(&timer_list);
            }
        }
    }

    return 0;
}

void timerctl_usage(const char* name)
{
    printf("usage: %s -amdi [ -C <count>] [ -c <command> ] [ -n <name> ] [ -t <mseconds> ]\n"
           "\t-a            : add new timer action\n"
           "\t-m            : change exist timer action\n"
           "\t-d            : delete exist timer action\n"
           "\t-C <count>    : delete this timer after <count> times triggered\n"
           "\t-c <command>  : timout action\n"
           "\t-n <name>     : timer name\n"
           "\t-t <mseconds> : interval\n"
           "\t-i <id>       : timerid\n", name);
    exit(0);
}

int timerctl_main(int argc, char** argv)
{
    int op;
    int ms;
    struct timer_ctl tc;
    char op_mask[32];

    memset(op_mask, 0, sizeof(op_mask));
    memset(&tc, 0, sizeof(tc));
    tc.action = TIMER_CTL_ACT_NONE;
    tc.timer.id = ~0;

    while(-1 != (op = getopt(argc, argv, "lamdi:C:c:n:t:"))){
        switch(op){
            case 'c':
                strncpy(tc.timer.command, optarg, sizeof(tc.timer.command)-1);
                break;
            case 'a':
                tc.action = TIMER_CTL_ACT_ADD;
                break;
            case 'm':
                tc.action = TIMER_CTL_ACT_MOD;
                break;
            case 'd':
                tc.action = TIMER_CTL_ACT_DEL;
                break;
            case 'l':
                tc.action = TIMER_CTL_ACT_DUMP;
                break;
            case 'i':
                tc.timer.id = atoi(optarg);
                break;
            case 't':
                ms = atoi(optarg);
                tc.timer.interval.tv_sec = ms / 1000;
                tc.timer.interval.tv_usec = (ms%1000) * 1000;
                break;
            case 'n':
                strncpy(tc.timer.name, optarg, sizeof(tc.timer.name)-1);
                break;
            case 's':
                tc.timer.count = atoi(optarg);
                break;
            case 'h':
            default:
                timerctl_usage(argv[0]);            
                break;
        }
    }
     
    if(tc.timer.count < 0 || ms < 0 || tc.timer.id < 0){
        return -EINVAL;
    }

    printf("check done\n");
    FILE* fp = fopen("/tmp/timerd.fifo", "w");
    if(!fp){
        return -errno;
    }
    printf("opened\n");
    flockfile(fp);
    printf("locked\n");
    fwrite(&tc, sizeof(tc), 1, fp);
    printf("write\n");
    funlockfile(fp);
    printf("unlocked\n");
    fclose(fp);

    return 0;
}

int main(int argc, char** argv)
{
    char* name = strdupa(argv[0]);
    name = basename(name);
    printf("%s\n", name);
    if(!strcmp(name, "timerctl")){
        printf("timerctl\n");
        return timerctl_main(argc, argv);
    }
    printf("timerd\n");
    return timerd_main(argc, argv);
}
