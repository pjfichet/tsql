/*
 * SQL - A SQLITE PREPROCESSOR FOR TROFF 
 *
 * Copyright (C) 2019 Pierre-Jean Fichet <pierrejean dot fichet at posteo dot net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sqlite3.h>
#include <math.h>

static int ln = 0;					/* line number */
static char cc = '.';				/* The control character */
static char * sqldb = "sqldb";		/* gets the database name */
static char * sqlds = "sqlds";		/* begins a .ds formatted query */
static char * sqlnr = "sqlnr";		/* begins a .nr formatted query */
static char * sqltbl = "sqltbl";	/* begins a tbl formatted query */
static char   sqltab = '\t';		/* default tbl separator */
static char * sqlbeg = "sqlbeg";	/* begins a table formatted query */
static char * sqlend = "sqlend";	/* ends a query */
static char * sqlso = "sqlso";	/* gets a statements filename */
static int sqlopenned = 0;			/* wether the database is openned or not */
static FILE *sqlfile;				/* the file sourced by sqlso */
sqlite3 *db;						/* the database pointer */

/* Add sqrt function to sqlite */
void sqlite_sqrt(sqlite3_context *context, int argc, sqlite3_value **argv) {
	double num = sqlite3_value_double(argv[0]); // get the first arg to the function
	double res = sqrt(num);                 // calculate the result
	sqlite3_result_double(context, res);        // save the result
}

/* add power function to sqlite */
void sqlite_power(sqlite3_context *context, int argc, sqlite3_value **argv) {
	double num = sqlite3_value_double(argv[0]); // get the first arg to the function
	double exp = sqlite3_value_double(argv[1]); // get the second arg
	double res = pow(num, exp);                 // calculate the result
	sqlite3_result_double(context, res);        // save the result
}

/* Open the sqlite database */
int sql_open(char *dbfile)
{
	int rc;
	rc = sqlite3_open(dbfile, &db);
	if(rc){
		fprintf(stderr, "Tsql error line %d. Can't open database %s.\n", ln, sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}
	if (sqlite3_create_function(db, "POWER", 2, SQLITE_UTF8, NULL, &sqlite_power, NULL, NULL)) {
		fprintf(stderr, "Tsql error line %d. Can't create power function.\n", ln);
		return 1;
	}
	if (sqlite3_create_function(db, "SQRT", 1, SQLITE_UTF8, NULL, &sqlite_sqrt, NULL, NULL)) {
		fprintf(stderr, "Tsql error line %d. Can't create sqrt function.\n", ln);
		return 1;
	}
	return 0;
}

/* Format the result as a string */
static int sql_str(void *notused, int argc, char **argv, char **colname)
{
	int i;
	for (i=0; i<argc; i++) {
		printf(" %s", argv[i] ? argv[i] : "");
	}
	printf("\n");
	return 0;
}

/* Format the result for tbl */
static int sql_tbl(void *notused, int argc, char **argv, char **colname)
{
	int i;
	printf("%s", argv[0] ? argv[0] : "");
	for (i=1; i<argc; i++) {
		printf("%c%s", sqltab, argv[i] ? argv[i] : "");
	}
	printf("\n");
	return 0;
}



/* Format the result as a table */
static int sql_beg(void *notused, int argc, char **argv, char **colname)
{
	int i;
	printf("%ctblrow\n", cc);
	for (i=0; i<argc; i++) {
		printf("%c\ttblcol %d\n%s\n", cc, i+1, argv[i] ? argv[i] : "");
	}
	return 0;
}

/* Executes an sqlite query */
int sql_exec(char *query, int (*callback)())
{
	int rc;
	char *errmsg = 0;
	rc = sqlite3_exec(db, query, callback, 0, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Tsql error line %d. SQL error: %s\n", ln, errmsg);
		sqlite3_free(errmsg);
		fprintf(stderr, "On query:\n %s\n", query);
		return 1;
	}
	return 0;
}

/* Close the sqlite database */
void sql_close()
{
	sqlite3_close(db);
}

/* get the next input line */
static char *lnget(void)
{
	static char buf[1024];
	ln++;
	return fgets(buf, sizeof(buf), stdin);
}

/* Return true if line begins with a known macro */
static int sql_mac(char *pat, char *line)
{
	int len = 256;
	char mac[len];
	int i = 0;
	if (*line++ != cc)
		return 0;
	while (*line && (*line	== ' ' || *line == '\t'))
		line++;
	for (i=0; i < len && *line && *line != '\n' && *line != ' '; i++)
		mac[i] = *line++;
	mac[i] = '\0';

	char *s = pat ? strstr(pat, mac) : NULL;
	if (!mac[0] || !s)
		return 0;
	return (s == pat && !s[strlen(mac)]);
}

/* Get the argument of a known macro */
static int sql_arg(char *line, char *arg, int len)
{
	int i = 0;
	if (*line++ != cc)
		return 1;
	while (*line && (*line == ' ' || *line == '\t'))
		line++;
	while (*line && *line != '\n' && *line != ' ' && *line != '\t')
		line++;
	while (*line && (*line == ' ' || *line == '\t'))
		line++;
	for (i=0; i < len && *line && *line != '\n' && *line != ' ' && *line != '\t'; i++)
		arg[i] = *line++;
	arg[i] = '\0';

	return 0;
}


/* reads and record query */
static int sql_query(int (*callback)())
{
	char *line;
	int bufsize = 1024;
	int querysize= 0;
	char *query = (char *)malloc(bufsize);
	if (query == NULL) {
		fprintf(stderr, "Tsql error line %d. Can not allocate memory.\n", ln);
		return 1;
	}

	while ((line = lnget())) {
		if (sql_mac(sqlend, line)) {
			break;
		}
		int len = strlen(line);
		while (bufsize < querysize + len + 1) {
			bufsize += 1024;
			query = (char*)realloc(query, bufsize);
		}
		memcpy(query + querysize, line, len);
		querysize += len;
	}
	query[querysize] = '\0';
	if (!sqlopenned) {
		fprintf(stderr, "Tsql error line %d. No sqlite3 file specified.\n", ln);
		return 1;
	}
	if (sql_exec(query, callback))
		return 1;
	free(query);
	return 0;
}

static int sql_file(char * filename)
{


	static char buf[1024];
	char *line;
	int bufsize = 1024;
	int querysize= 0;
	char *query = (char *)malloc(bufsize);
	if (query == NULL) {
		fprintf(stderr, "Tsql error line %d. Can not allocate memory.\n", ln);
		return 1;
	}

	sqlfile = fopen(filename, "r");
	if (!sqlfile)
		return 1;
	
	while ((line = fgets(buf, sizeof(buf), sqlfile))) {
		int len = strlen(line);
		while (bufsize < querysize + len + 1) {
			bufsize += 1024;
			query = (char*)realloc(query, bufsize);
		}
		memcpy(query + querysize, line, len);
		querysize += len;
	}
	query[querysize] = '\0';
	if (!sqlopenned) {
		fprintf(stderr, "Tsql error line %d. No sqlite3 file specified.\n", ln);
		return 1;
	}
	if (sql_exec(query, sql_beg))
		return 1;
	free(query);
	fclose(sqlfile);
	return 0;
}


/* Parse the input file and replace queries with their result */
static int sql(void)
{
	char *line;
	int len = 256;
	char arg[len];
	while ((line = lnget())) {
		/* .sqldb <database> */
		if (sql_mac(sqldb, line)) {
			if (sql_arg(line, arg, len) || arg[0] == '\n') {
				fprintf(stderr, "Tsql error line %d. Missing argument.\n", ln);
				return 1;
			}
			if (sqlopenned)
				sql_close();
			sqlopenned = 1;
			if (sql_open(arg))
				return 1;
			continue;
		}	
		/* .sqlds <string> <query> .sqlend */
		if (sql_mac(sqlds, line)) {
			if (sql_arg(line, arg, len) || arg[0] == '\n') {
				fprintf(stderr, "Tsql error line %d. Missing argument.\n", ln);
				return 1;
			}
			printf("%cds %s", cc, arg);
			if (sql_query(sql_str))
				return 1;
			continue;
		}
		/* .sqlnr <register> <query> .sqlend */
		if (sql_mac(sqlnr, line)) {
			if (sql_arg(line, arg, len) || arg[0] == '\n') {
				fprintf(stderr, "Tsql error line %d. Missing argument.\n", ln);
				return 1;
			}
			printf("%cnr %s", cc, arg);
			if (sql_query(sql_str))
				return 1;
			continue;
		}
		/* .sqltbl <query> .sqlend */
		if (sql_mac(sqltbl, line)) {
			if (!sql_arg(line, arg, len) && arg[0] != '\0')
				sqltab = arg[0];
			if (sql_query(sql_tbl))
				return 1;
			continue;
		}
		/* .sqlbeg <query> .sqlend */
		if (sql_mac(sqlbeg, line)) {
			if (sql_query(sql_beg))
				return 1;
			continue;
		}
		/* .sqlfile <file> */
		if (sql_mac(sqlso, line)) {
			if (sql_arg(line, arg, len) || arg[0] == '\n') {
				fprintf(stderr, "Tsql error line %d. Missing filename.\n", ln);
				return 1;
			}
			if (sql_file(arg))
				return 1;
			continue;
		}
		printf("%s", line);
		continue;
	}
	sql_close();
	return 0;
}

static char *usage =
	"Usage neatrefer [options] <input >output\n"
	"Options:\n"
	"\t-c    \tspecify the control character (default '.')\n";

int main(int argc, char *argv[])
{
	int i;
	for (i = 1; i < argc; i++) {
		switch (argv[i][0] == '-' ? argv[i][1] : 'h') {
		case 'c':
			cc = argv[i][2] ? argv[i][0] + 2 : argv[++i][0];
			break;
		default:
			printf("%s", usage);
			return 1;
		}
	}
	return sql();
}
