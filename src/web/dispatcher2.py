#!/usr/bin/python
# Author: Samuel Sekiwere <sekiskylink@gmail.com>

import os
import sys
import web
import urllib
import logging
# import re
# import time
# from datetime import datetime
# from datetime import timedelta
import parsedatetime
from web.contrib.template import render_jinja

filedir = os.path.dirname(__file__)
sys.path.append(os.path.join(filedir))
from pagination import doquery, getPaginationString, countquery

cal = parsedatetime.Calendar()


class AppURLopener(urllib.FancyURLopener):
    version = "dispatcher2 /1.0"

urllib._urlopener = AppURLopener()

logging.basicConfig(
    format='%(asctime)s:%(levelname)s:%(message)s', filename='/var/log/dispatcher/dispatcher2-web.log',
    datefmt='%Y-%m-%d %I:%M:%S', level=logging.DEBUG
)

# DB confs
db_host = 'localhost'
db_name = 'skytools'
db_user = 'postgres'
db_passwd = 'postgres'

urls = (
    "/", "Index",
    "/info", "Info",
    "/failed", "Failed",
    "/completed", "Completed",
    "/requests", "Requests",
    "/ready", "Ready",
    "/users", "Admin",
    "/logout", "Logout"
)

# web.config.smtp_server = 'mail.mydomain.com'
web.config.debug = False

app = web.application(urls, globals())
db = web.database(
    dbn='postgres',
    user=db_user,
    pw=db_passwd,
    db=db_name,
    host=db_host
)

store = web.session.DBStore(db, 'sessions')
session = web.session.Session(app, store, initializer={'loggedin': False})

render = render_jinja(
    'templates',
    encoding='utf-8'
)
render._lookup.globals.update(
    ses=session
)

SETTINGS = {
    'PAGE_LIMIT': 25,
}


def lit(**keywords):
    return keywords


def default(*args):
    p = [i for i in args if i or i == 0]
    if p.__len__():
        return p[0]
    if args.__len__():
        return args[args.__len__() - 1]
    return None


def auth_user(db, username, password):
    sql = (
        "SELECT id,firstname,lastname FROM users WHERE username = '%s' AND password = "
        "crypt('%s', password)")
    res = db.query(sql % (username, password))
    if not res:
        return False, "Wrong username or password"
    else:
        return True, res[0]


def require_login(f):
    """usage
    @require_login
    def GET(self):
        ..."""
    def decorated(*args, **kwargs):
        if not session.loggedin:
            session.logon_err = "Please Logon"
            return web.seeother("/")
        else:
            session.logon_err = ""
        return f(*args, **kwargs)

    return decorated


class Index:
    def GET(self):
        l = locals()
        del l['self']
        return render.start(**l)

    def POST(self):
        global session
        params = web.input(username="", password="")
        username = params.username
        password = params.password
        r = auth_user(db, username, password)
        if r[0]:
            session.loggedin = True
            info = r[1]
            session.username = info.firstname + " " + info.lastname
            session.sesid = info.id
            # requests = db.query("SELECT * FROM requests order by id desc")
            # all_requests = {}
            # for request in all_requests:
            #     all_requests[request.id] = request.submissionid
            # session.all_requests = all_requests

            l = locals()
            del l['self']
            return web.seeother("/requests")
        else:
            session.loggedin = False
            session.logon_err = r[1]
        l = locals()
        del l['self']
        return render.logon(**l)


class Requests:
    @require_login
    def GET(self):
        params = web.input(page=1)
        try:
            page = int(params.page)
        except:
            page = 1

        limit = SETTINGS['PAGE_LIMIT']
        start = (page - 1) * limit if page > 0 else 0

        dic = lit(relations='requests', fields="*", criteria="", order="id desc", limit=limit, offset=start)
        res = doquery(db, dic)
        count = countquery(db, dic)
        pagination_str = getPaginationString(default(page, 0), count, limit, 2, "requests", "?page=")

        l = locals()
        del l['self']
        return render.requests(**l)


class Failed:
    @require_login
    def GET(self):
        params = web.input(page=1)
        try:
            page = int(params.page)
        except:
            page = 1

        limit = SETTINGS['PAGE_LIMIT']
        start = (page - 1) * limit if page > 0 else 0

        # we start getting requests a month old
        t = cal.parse("2 month ago")[0]
        amonthAgo = '%s-%s-%s' % (t.tm_year, t.tm_mon, t.tm_mday)

        dic = lit(
            relations='requests', fields="*",
            criteria="status='failed' AND cdate > '%s' AND xml_is_well_formed(request_body)" % (amonthAgo),
            order="id desc",
            limit=limit, offset=start)
        res = doquery(db, dic)
        count = countquery(db, dic)
        pagination_str = getPaginationString(default(page, 0), count, limit, 2, "failed", "?page=")

        l = locals()
        del l['self']
        return render.failed(**l)

    @require_login
    def POST(self):
        params = web.input(page=1, reqid=[])
        print params
        try:
            page = int(params.page)
        except:
            page = 1

        with db.transaction():
            if params.pbtn == 'Retry Selected':
                if params.reqid:
                    for val in params.reqid:
                        db.update('requests', where="id = %s" % val, status='ready')
            if params.pbtn == 'Delete Selected':
                if params.reqid:
                    for val in params.reqid:
                        db.delete('requests', where="id = %s" % val)
            db.transaction().commit()

        limit = SETTINGS['PAGE_LIMIT']
        start = (page - 1) * limit if page > 0 else 0

        # we start getting requests a month old
        t = cal.parse("2 month ago")[0]
        amonthAgo = '%s-%s-%s' % (t.tm_year, t.tm_mon, t.tm_mday)

        dic = lit(
            relations='requests', fields="*",
            criteria="status='failed' AND cdate > '%s' AND xml_is_well_formed(request_body)" % (amonthAgo),
            order="id desc",
            limit=limit, offset=start)
        res = doquery(db, dic)
        count = countquery(db, dic)
        pagination_str = getPaginationString(default(page, 0), count, limit, 2, "failed", "?page=")

        l = locals()
        del l['self']
        return render.failed(**l)


class Completed:
    @require_login
    def GET(self):
        params = web.input(page=1)
        try:
            page = int(params.page)
        except:
            page = 1

        limit = SETTINGS['PAGE_LIMIT']
        start = (page - 1) * limit if page > 0 else 0

        dic = lit(
            relations='requests', fields="*",
            criteria="status='completed'",
            order="id desc",
            limit=limit, offset=start)
        res = doquery(db, dic)
        count = countquery(db, dic)
        pagination_str = getPaginationString(default(page, 0), count, limit, 2, "completed", "?page=")

        l = locals()
        del l['self']
        return render.completed(**l)


class Ready:
    @require_login
    def GET(self):
        params = web.input(page=1)
        try:
            page = int(params.page)
        except:
            page = 1

        limit = SETTINGS['PAGE_LIMIT']
        start = (page - 1) * limit if page > 0 else 0

        dic = lit(
            relations='requests', fields="*",
            criteria="status='ready'",
            order="id desc",
            limit=limit, offset=start)
        res = doquery(db, dic)
        count = countquery(db, dic)
        pagination_str = getPaginationString(default(page, 0), count, limit, 2, "ready", "?page=")

        l = locals()
        del l['self']
        return render.ready(**l)


class Users:
    @require_login
    def GET(self):
        l = locals()
        del l['self']
        return render.users(**l)

    def POST(self):
        params = web.input()
        l = locals()
        del l['self']
        return render.users(**l)


class Settings:
    @require_login
    def GET(self):
        l = locals()
        del l['self']
        return render.settings(**l)

    def POST(self):
        params = web.input()
        l = locals()
        del l['self']
        return render.settings(**l)


class Logout:
    def GET(self):
        session.kill()
        return web.seeother("/")

if __name__ == "__main__":
    app.run()

# makes sure apache wsgi sees our app
application = web.application(urls, globals()).wsgifunc()
