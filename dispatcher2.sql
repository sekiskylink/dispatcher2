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
    year int, -- year of submission
    ldate timestamptz DEFAULT current_timestamp,
    cdate timestamptz DEFAULT current_timestamp
);

CREATE INDEX requests_idx1 ON requests(submissionid);
CREATE INDEX requests_idx2 ON requests(status);
CREATE INDEX requests_idx3 ON requests(statuscode);

INSERT INTO servers (name, username, password, ipaddress, url, auth_method)
    VALUES
        ('localhost', 'tester', 'foobar', '127.0.0.1', 'http://localhost:8080/test', 'Basic'),
        ('dhis2', 'tester', 'foobar', '127.0.0.1', 'http://hmis2.health.go.ug/api/dataValueSets', 'Basic');
