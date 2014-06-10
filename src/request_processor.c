#include <gwlib/gwlib.h>
#include <unistd.h>
#include <ctype.h>
#include <stdint.h>
#include <time.h>
#include <libpq-fe.h>
#include <libxml/xmlmemory.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include "request_processor.h"

static dispatcher2conf_t dispatcher2conf;
static List *srvlist;

static xmlChar *findvalue(xmlDocPtr doc, xmlChar *xpath, int add_namespace){
    xmlNodeSetPtr nodeset;
    xmlChar * value;
    xmlXPathContextPtr context;
    xmlXPathObjectPtr result;
    int i;

    if(!doc){
        info(0, "Null Doc for %s", xpath);
        return NULL;
    }
    context = xmlXPathNewContext(doc);
    if(!context){
        info(0, "Null context for %s", xpath);
        return NULL;
    }

    if (add_namespace){
        xmlChar *prefix = (xmlChar *) "xmlns";
        xmlChar *href = (xmlChar *) "http://dhis2.org/schema/dxf/2.0";
        if (xmlXPathRegisterNs(context, prefix, href) != 0){
            info(0, "Unable to register NS:%s", prefix);
            return NULL;
        }
    }
    result = xmlXPathEvalExpression(xpath, context);
    if(!result){
        info(0, "Null result for %s", xpath);
        return NULL;
    }
    if(xmlXPathNodeSetIsEmpty(result->nodesetval)){
        xmlXPathFreeObject(result);
        info(0, "xmlXPathNodeSetIsEmpty for %s", xpath);
        return NULL;
    }
    nodeset = result->nodesetval;
    /* We return first match */
    for (i=0; i < nodeset->nodeNr; i++) {
        value = xmlNodeListGetString(doc, nodeset->nodeTab[i]->xmlChildrenNode, 1);
        if(value)
            break;
    }
    xmlXPathFreeContext(context);
    xmlXPathFreeObject(result);

    return value;
}

#define REQUEST_SQL "SELECT id FROM requests WHERE status = 'ready'"

static void init_request_processor_sql(PGconn *c)
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
static Octstr *post_xmldata_to_server(PGconn *c, int serverid, Octstr *data) {
    PGresult *r;
    char tmp[64], *x;
    Octstr *url = NULL, *user = NULL, *passwd = NULL;
    const char *pvals[] = {tmp};
    HTTPCaller *caller;

    List *request_headers;
    Octstr *furl = NULL, *rbody = NULL;
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

    /*  request_headers = http_create_empty_headers();*/
    request_headers = gwlist_create();
    http_header_add(request_headers, "Content-Type", "application/xml");
    http_add_basic_auth(request_headers, user, passwd);

    caller = http_caller_create();
    http_start_request(caller, method, url, request_headers, data, 1, NULL, NULL);
    http_receive_result_real(caller, &status, &furl, &request_headers, &rbody, 1);

    http_caller_destroy(caller);
    http_destroy_headers(request_headers);
    octstr_destroy(user);
    octstr_destroy(passwd);
    octstr_destroy(url);
    octstr_destroy(furl);
    /*  octstr_destroy(rbody); */

    if (status == -1){
        if(rbody) octstr_destroy(rbody);
        return NULL;
    }
    return rbody;
}

static void do_request(PGconn *c, int64_t rid) {
    char tmp[64], *cmd, *x, buf[256], st[64];
    PGresult *r;
    int retries, serverid;
    /* int64_t submissionid; */
    Octstr *data;
    const char *pvals[] = {tmp, st, buf};
    Octstr *resp;
    xmlDocPtr doc;
    xmlChar *s, *im, *ig, *up;

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
    info(0, "Post Data %s", octstr_get_cstr(data));

    if (retries > dispatcher2conf->max_retries) {
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "status = 'expired' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        return;
    }

    if (data == NULL){
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "statuscode='ERROR1', status = 'failed' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        /* Mark this one as failed*/
        return;
    }

    resp = post_xmldata_to_server(c, serverid, data);

    if (resp == NULL) {
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "statuscode = 'ERROR2', status = 'failed' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        return;
    }
    info(0, "Response Data %s", octstr_get_cstr(resp));
    /* parse response - hopefully it is xml */
    doc = xmlParseMemory(octstr_get_cstr(resp), octstr_len(resp));

    if (doc == NULL) {
        r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
                "statuscode = 'ERROR3',status = 'failed' WHERE id = $1",
                1, NULL, pvals, NULL, NULL, 0);
        PQclear(r);
        return;
    }

    s = findvalue(doc, (xmlChar *)"//xmlns:status", 1); /* third arg is 0 if no namespace required*/
    im = findvalue(doc, (xmlChar *)"//xmlns:dataValueCount[1]/@imported", 1);
    ig = findvalue(doc, (xmlChar *)"//xmlns:dataValueCount[1]/@ignored", 1);
    up = findvalue(doc, (xmlChar *)"//xmlns:dataValueCount[1]/@updated", 1);

    sprintf(st, "%s", s);
    sprintf(buf, "Imp:%s Ign:%s Up:%s",im, ig, up);
    if(s) xmlFree(s);
    if(im) xmlFree(im);
    if(ig) xmlFree(ig);
    if(up) xmlFree(up);

    r = PQexecParams(c, "UPDATE requests SET ldate = timeofday()::timestamp, "
            "statuscode=$2, status = 'completed', errmsg = $3 WHERE id = $1",
            3, NULL, pvals, NULL, NULL, 0);
    PQclear(r);

    octstr_destroy(resp);
    if(doc)
        xmlFreeDoc(doc);
}

static void request_run(PGconn *c) {
    int64_t *rid;
    dispatcher2conf_t config = dispatcher2conf;

    if (srvlist != NULL)
        gwlist_add_producer(srvlist);
    while((rid = gwlist_consume(req_list)) != NULL) {
        PGresult *r;
        char tmp[64];
        Octstr *xkey;
        int64_t xid = *rid;

        time_t t = time(NULL);
        struct tm tm = gw_localtime(t);

        if (!(tm.tm_hour >= config->start_submission_period
                    && tm.tm_hour <= config->end_submission_period)){
            /* warning(0, "We're out of submission period"); */
            gwthread_sleep(config->request_process_interval);
            continue; /* we're outide submission period so stay silent*/
        }

        /* heal connection if bad*/
        r = PQexec(c, "BEGIN");
        PQclear(r);

        sprintf(tmp, "%lld", xid);
        xkey = octstr_format("Request-%s", tmp);
        do_request(c, xid);

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
    gwlist_add_producer(req_list);

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
        time_t t = time(NULL);
        struct tm tm = gw_localtime(t);

        if (!(tm.tm_hour >= config->start_submission_period
                    && tm.tm_hour <= config->end_submission_period)){
            /* warning(0, "We're out of submission period"); */
            gwthread_sleep(config->request_process_interval);
            continue; /* we're outide submission period so stay silent*/
        }

        gwthread_sleep(config->request_process_interval);
        /*info(0, "We got here ###############%ld\n", gwlist_len(req_list));*/

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
                rid = y && isdigit(y[0]) ? strtoul(y, NULL, 10) : 0;

                int64_t *x = gw_malloc(sizeof *x);
                *x = rid;

                gwlist_produce(req_list, x);
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

void stop_request_processor(void)
{

     qstop = 1;
     gwthread_sleep(2); /* Give them some time */
     gwthread_wakeup(rthread_th);

     gwthread_sleep(2); /* Give them some time */
     gwthread_join(rthread_th);

     info(0, "Request processor shutdown complete");
}

