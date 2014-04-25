CREATE TABLE servers(
    id serial PRIMARY KEY NOT NULL,
    name text NOT NULL,
    username text NOT NULL DEFAULT '',
    password text NOT NULL DEFAULT '',
    ipaddress text NOT NULL DEFAULT '',
    url text NOT NULL DEFAULT '',
    auth_method text NOT NULL DEFAULT '',
    cdate timestamptz DEFAULT current_timestamp
);

--CREATE TABLE server_credentials(
--    id SERIAL PRIMARY KEY NOT NULL,
--    serverid INTEGER REFERENCES servers(id),
--    username TEXT NOT NULL DEFAULT '',
--    password TEXT NOT NULL DEFAULT '',
--    ipaddress TEXT NOT NULL DEFAULT '',
--    url TEXT NOT NULL DEFAULT '',
--    auth_method TEXT NOT NULL DEFAULT '',
--    cdate TIMESTAMP DEFAULT CURRENT_TIMESTAMP
--);

CREATE TABLE requests(
    id bigserial PRIMARY KEY NOT NULL,
    serverid integer REFERENCES servers(id),
    request_body TEXT NOT NULL DEFAULT '',
    status varchar(32) NOT NULL DEFAULT 'ready' CHECK( status IN('ready', 'inprogress', 'failed', 'error', 'expired', 'completed')),
    statuscode text DEFAULT '',
    retries integer NOT NULL DEFAULT 0,
    errmsg text DEFAULT '',
    submissionid integer NOT NULL DEFAULT 0, -- helpful when check for already sent submissions
    week text DEFAULT'',
    ldate timestamptz DEFAULT current_timestamp,
    cdate timestamptz DEFAULT current_timestamp
);

INSERT INTO servers (name, username, password, ipaddress, url, auth_method)
    VALUES
        ('localhost', 'tester', 'foobar', '127.0.0.1', 'http://localhost:8080/', 'Basic');
