#ifndef DISPATCHER2_H
#define DISPATCHER2_H
/* dispatcher2 server
 * Created:  04/09/2014 08:12:19 PM
 * Author: Samuel Sekiwere <sekiskylink@gmail.com>
 *
 */
#include "gwlib/gwlib.h"
#include <libpq-fe.h>

struct HTTPData {
    Octstr *url;
    HTTPClient *client;
    Octstr *body;
    List *cgivars;
    Octstr *ip;
    List *reqh;
    PGconn *dbconn;
};

void free_HTTPData(struct HTTPData *x, int free_enclosed);

#define NELEMS(a) (sizeof (a))/(sizeof (a)[0])
#define TEST_URL "/test"
#define NUM_DBS 2

struct dispatcher2conf{
    int http_port;
    int num_threads;
    char db_conninfo[512];
    double request_process_interval;
    int max_retries;
    int start_submission_period;
    int end_submission_period;
};

typedef struct dispatcher2conf *dispatcher2conf_t;
#endif
