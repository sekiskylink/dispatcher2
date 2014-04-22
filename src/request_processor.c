#include <gwlib/gwlib.h>
#include <unistd.h>
#include <ctype.h>
#include <libpq-fe.h>

#include "request_processor.h"

static dispatcher2conf_t dispatcher2conf;
static List *srvlist;
static HTTPCaller *caller;

#define REQUEST_SQL "SELECT id FROM requests WHERE status = 'ready'"

void init_request_processor_sql(PGconn *c)
{
    PGresult *r;
    if (PQstatus(c) != CONNECTION_OK)
        return;

    r = PQprepare(c, "REQUEST_SQL", REQUEST_SQL, 0, NULL);
    PQclear(r);
}

static List *req_list; /* Of Request id */
static Dict *req_dict; /* For keeping list short*/

/* Post XML to server using basic auth and return response */
Octstr * post_xmldata_to_server(PGconn *c, int serverid, Octstr *data) {
    PGresult *r;
    char tmp[64], *x;
    Octstr *url = NULL, *user = NULL, *passwd = NULL;
    const char *pvals[] = {tmp};

    List *request_headers;
    Octstr *furl = NULL, *rbody = NULL, *body = octstr_imm("");
    int method = HTTP_METHOD_POST;
    int status = -1;


    sprintf(tmp, "%d", serverid);
    r = PQexecParams(c,
            "SELECT username, password, url FROM servers WHERE id = $1",
            1, NULL, pvals, NULL, NULL, 0);

    if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) <=0) {
        PQclear(r);
        return NULL;
    }

    x = PQgetvalue(r, 0, 0);
    user = (x && x[0]) ? octstr_create(x) : NULL;
    x = PQgetvalue(r, 0, 1);
    passwd = (x && x[0]) ? octstr_create(x) : NULL;
    x = PQgetvalue(r, 0, 2);
    url = (x && x[0]) ? octstr_create(x) : NULL;
    PQclear(r);

    request_headers = http_create_empty_headers();
    http_header_add(request_headers, "Content-Type", "text/xml; charset=\"ISO-8859-1\"");
    http_add_basic_auth(request_headers, user, passwd);

    caller = http_caller_create();
    http_start_request(caller, method, url, request_headers, body, 1, NULL, NULL);
    http_receive_result_real(caller, &status, &furl, &request_headers, &rbody, 1);

    http_caller_destroy(caller);
    http_destroy_headers(request_headers);
    octstr_destroy(user);
    octstr_destroy(passwd);
    octstr_destroy(url);
    octstr_destroy(body);
    octstr_destroy(furl);
    /*  octstr_destroy(rbody); */

    if (status == -1)
        return NULL;
    return rbody;
}

void do_request(PGconn *c, int64_t rid) {
    char tmp[64], *cmd, *x, buf[256];
    PGresult *r;
    int nargs, retries, serverid;
    int64_t submissionid;
    Octstr *data;
    const char *pvals[] = {tmp, buf};
    Octstr *resp;

    sprintf(tmp, "%lld", rid);

    /* NOWAIT forces failure if the record is locked. Also we do not process if not yet time.*/
    cmd = "SELECT serverid, request_body, retries, submissionid FROM requests WHERE id = $1 FOR UPDATE NOWAIT";

    r = PQexecParams(c, cmd, 1, NULL, pvals, NULL, NULL, 0);
    if (PQresultStatus(r) != PGRES_TUPLES_OK || PQntuples(r) <= 0) {
        /*skip this one*/
        PQclear(r);
        return;
    }

    serverid = (x = PQgetvalue(r, 0, 0)) != NULL ? atoi(x) : -1;
    retries = (x = PQgetvalue(r, 0, 2)) != NULL ? atoi(x) : -1;
    x = PQgetvalue(r, 0, 1);
    data = (x && x[0]) ? octstr_create(x) : NULL; /* POST XML*/
    PQclear(r);
    if (data == NULL){
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "status = 'failed' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        /* Mark this one as failed*/
        return;
    }

    resp = post_xmldata_to_server(c, serverid, data);

    if (resp == NULL) {
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "status = 'failed' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        return;
    }
    /* parse response */

    octstr_destroy(resp);
}

static void request_run(PGconn *c) {
    int64_t *rid;
    if (srvlist != NULL)
        gwlist_add_producer(srvlist);
    while((rid = gwlist_consume(req_list)) != 0) {
        PGresult *r;
        char tmp[64];
        Octstr *xkey;
        /* heal connection if bad*/
        r = PQexec(c, "BEGIN");
        PQclear(r);

        sprintf(tmp, "%lld", *rid);
        xkey = octstr_format("Request-%s", tmp);
        do_request(c, *rid);

        r = PQexec(c, "COMMIT");
        PQclear(r);

        gw_free(rid);
        dict_remove(req_dict, xkey);
        octstr_destroy(xkey);
    }
    PQfinish(c);
    if (srvlist != NULL)
        gwlist_remove_producer(srvlist);

}

static int qstop = 0;

#define HIGH_WATER_MARK 100000
#define MAX_QLEN 100000
static void run_request_processor(PGconn *c)
{
    /* Start worker threads
     * Produce jobs in the server_req_list
     * */
    int i, num_threads;
    dispatcher2conf_t config = dispatcher2conf;

    info(0, "Request processor starting up...");

    req_dict = dict_create(config->num_threads * MAX_QLEN + 1, NULL);
    req_list = gwlist_create();

    for (i = num_threads = 0; i<config->num_threads; i++) {
        PGconn *conn = PQconnectdb(config->db_conninfo);

        if (PQstatus(conn) != CONNECTION_OK) {
            error(0, "request_processor: Failed to connect to database: %s",
		     PQerrorMessage(conn));
	        PQfinish(conn);
        } else {
            gwthread_create((void *)request_run, conn);
            num_threads++;
        }
    }

    if (num_threads == 0)
        goto finish;

    do {
        PGresult *r;
        long i, n;

        gwthread_sleep(config->request_process_interval);

        if (qstop)
            break;
        else if ((n = gwlist_len(req_list)) > HIGH_WATER_MARK) {
            warning(0, "Request processor: Too many (%ld) pending batches, will wait a little", n);
            continue;
        }

        if (PQstatus(c) != CONNECTION_OK) {
            /* Die...for real*/
            panic(0, "Request processor: Failed to connect to database: %s",
                    PQerrorMessage(c));
            PQfinish(c);

            c= PQconnectdb(config->db_conninfo);
            init_request_processor_sql(c);
            goto loop;
        }
        r = PQexecPrepared(c, "REQUEST_SQL", 0, NULL, NULL, NULL, 0);
        n = PQresultStatus(r) == PGRES_TUPLES_OK ? PQntuples(r) : 0;
        for (i=0; i<n; i++) {
            char *y = PQgetvalue(r, i, 0);
            Octstr *xkey = octstr_format("Request-%s", y);
            if (dict_put_once(req_dict, xkey, (void*)1) == 1) { /* Item not in queue waiting*/
                int64_t rid;
                rid = y && isdigit(y[0]) ? strtoul(y, NULL, 10) : -1;

                gwlist_produce(req_list, &rid);
                octstr_destroy(xkey);
            }
        }
        PQclear(r);
    loop:
        (void)0;
    } while (qstop == 0);

finish:
    PQfinish(c);
    gwlist_remove_producer(req_list);
    gwthread_join_every((void *)request_run);
    gwlist_destroy(req_list, NULL);
    info(0, "Request processor exited!!!");
    dict_destroy(req_dict);
}

static long rthread_th = -1;
void start_request_processor(dispatcher2conf_t config, List *server_req_list)
{
    PGconn *c;

    dispatcher2conf = config;
    c = PQconnectdb(config->db_conninfo);
    if (PQstatus(c) != CONNECTION_OK) {
        error(0, "Request processor: Failed to connect to database: %s",
		PQerrorMessage(c));
        return;
    }

    init_request_processor_sql(c);

    srvlist = server_req_list;
    rthread_th = gwthread_create((void *) run_request_processor, c);
}
