/* dispatcher2 Server
 * Created:  04/09/2014 03:38:35 PM
 * Author: Samuel Sekiwere <sekiskylink@gmail.com>
 *
 */
#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include "dispatcher2.h"
#include "request_processor.h"

#define HTTP_PORT 9090
#define NUM_THREADS 5

static int stop = 0;

/*URLs and their handlers*/
typedef const char *(*request_handler_t)(Octstr *rh, struct HTTPData *x, Octstr *rbody, int *status);
static request_handler_t uri2handler(Octstr *);
static int supporteduri(Octstr *);
static void dispatch_processor(void *data);
static void dispatch_request(struct HTTPData *x);

static struct {
    char *uri;
    request_handler_t func;

} uri_funcs[] = {
    {TEST_URL, NULL},
    /* {"/sendsms", sendsms}, */
};



struct dispatcher2conf config = {
    9090,
    5,
    "host=localhost dbname=skytools user=postgres password=postgres",
    45,
    3,
    7, /*hours*/
    23
};

static List *server_req_list;

int main(int argc, char *argv[])
{
    List *rh = NULL, *cgivars = NULL;
    Octstr *ip = NULL, *url = NULL, *body = NULL, *test_url;
    int i;

    gwlib_init();
    HTTPClient *client = NULL;

    signal(SIGPIPE, SIG_IGN); /* Ignore piping us*/

    if (http_open_port(config.http_port, 0) < 0)
        panic(0,"Dispatcher2 Failed to open port %d: %s!", config.http_port, strerror(errno));

    server_req_list = gwlist_create();
    gwlist_add_producer(server_req_list);

    start_request_processor(&config, server_req_list);

    /*We start processor threads to handle the HTTP request we get*/
    for(i = 0; i < config.num_threads; i++)
        gwthread_create((gwthread_func_t *) dispatch_processor, server_req_list);

    info(0, "Entering Processing loop");
    while (!stop && (client = http_accept_request(config.http_port, &ip, &url, &rh, &body, &cgivars)) != NULL)
    {
        info(0,"dispatcher2 Incoming Request [IP = %s] [URI=%s]",octstr_get_cstr(ip), octstr_get_cstr(url));
        struct HTTPData *x;
        x = gw_malloc(sizeof *x);
        memset(x, 0, sizeof *x);
        x->url = url;
        x->client = client;
        x->ip = ip;
        x->body = body;
        x->reqh = rh;
        x->cgivars = cgivars;

        gwlist_produce(server_req_list, x);
    }

    stop_request_processor();

    gwlist_remove_producer(server_req_list);
    gwthread_join_every((void *)dispatch_processor);
    info(0, "Dispatcher shutdown complete");

    gwlist_destroy(server_req_list, NULL);


    gwlib_shutdown();
    return 0;
}	/* ----------  end of function main  ---------- */

static request_handler_t uri2handler(Octstr *uri) {
    int i;
    for(i = 0; i<NELEMS(uri_funcs); i++)
        if (octstr_str_case_compare(uri, uri_funcs[i].uri) == 0)
            return uri_funcs[i].func;
    return NULL;
}

static int supporteduri(Octstr *uri){
    int i;
    for(i =0; i<NELEMS(uri_funcs); i++)
        if (octstr_str_case_compare(uri, uri_funcs[i].uri) == 0)
            return 1;
    return 0;
}

static void dispatch_request(struct HTTPData  *x) {
    int status = HTTP_OK;
    Octstr *rbody = NULL;
    List *rh;
    const char *cmd = "";
    request_handler_t func;

    rh = http_create_empty_headers();
    rbody = octstr_create("");

    if ((func = uri2handler(x->url)) != NULL){
        cmd = func(rh, x, rbody, &status);
    }

    http_header_add(rh, "Server", "SamGw");
    if (x->client != NULL)
        http_send_reply(x->client, status, rh, rbody);

    http_destroy_headers(rh);
    octstr_destroy(rbody);

}

void free_HTTPData(struct HTTPData *x, int free_enclosed)
{
     octstr_destroy(x->body);
     octstr_destroy(x->url);
     octstr_destroy(x->ip);

     if (x->reqh)
	  http_destroy_headers(x->reqh);
     if (x->cgivars)
	  http_destroy_cgiargs(x->cgivars);
     /*
     http_destroy_cgiargs(x->cgi_ctypes);
     */

     if (free_enclosed)
	  gw_free(x);
}


static void dispatch_processor(void *data)
{
    struct HTTPData *x;
    List *req_list = (List *)data;
    PGconn *c = PQconnectdb(config.db_conninfo);

    if (PQstatus(c) != CONNECTION_OK) {
        error(0, "Failed to connect to database: %s",
                PQerrorMessage(c));
        PQfinish(c);
        c = NULL;
    }

    while ((x = gwlist_consume(req_list)) != NULL) {
        x->dbconn = c;
        if (c) {
            PGresult *r = PQexec(c, "BEGIN;"); /*transactionize*/
            PQclear(r);
        }
        dispatch_request(x);
        if (c) {
            PGresult *r = PQexec(c, "COMMIT");
            PQclear(r);
        }
    }
    if (c != NULL)
        PQfinish(c);
}
