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

/* Constants and globals */
#define BUFFER_SIZE 1024
static int ln = 0;
static char cc = '.';
static char *sql_keywords[] = {"sqldb", "sqlds", "sqlnr", "sqltbl", "sqlbeg", "sqlend", "sqlso"};
static char sqltab = '\t';
static int sqlopened = 0;
static FILE *sqlfile;
sqlite3 *db;

/* ---------- SQLite Custom Functions ---------- */

void sqlite_sqrt(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
	double num = sqlite3_value_double(argv[0]);
	sqlite3_result_double(ctx, sqrt(num));
}

void sqlite_power(sqlite3_context *ctx, int argc, sqlite3_value **argv) {
	double base = sqlite3_value_double(argv[0]);
	double exp  = sqlite3_value_double(argv[1]);
	sqlite3_result_double(ctx, pow(base, exp));
}

/* ---------- SQLite Setup ---------- */

int sql_open(const char *dbfile) {
	if (sqlite3_open(dbfile, &db)) {
		fprintf(stderr, "Line %d: Can't open database %s.\n", ln, sqlite3_errmsg(db));
		sqlite3_close(db);
		return 1;
	}
	if (sqlite3_create_function(db, "POWER", 2, SQLITE_UTF8, NULL, sqlite_power, NULL, NULL) ||
		sqlite3_create_function(db, "SQRT", 1, SQLITE_UTF8, NULL, sqlite_sqrt, NULL, NULL)) {
		fprintf(stderr, "Line %d: Failed to register custom functions.\n", ln);
		return 1;
	}
	return 0;
}

void sql_close() {
	if (db) sqlite3_close(db);
}

/* ---------- Callback Functions ---------- */

int sql_str(void *unused, int argc, char **argv, char **colname) {
	for (int i = 0; i < argc; i++)
		printf(" %s", argv[i] ? argv[i] : "");
	printf("\n");
	return 0;
}

int sql_tbl(void *unused, int argc, char **argv, char **colname) {
	printf("%s", argv[0] ? argv[0] : "");
	for (int i = 1; i < argc; i++)
		printf("%c%s", sqltab, argv[i] ? argv[i] : "");
	printf("\n");
	return 0;
}

int sql_beg(void *unused, int argc, char **argv, char **colname) {
	printf("%ctblrow\n", cc);
	for (int i = 0; i < argc; i++)
		printf("%c\ttblcol %d\n%s\n", cc, i + 1, argv[i] ? argv[i] : "");
	return 0;
}

/* ---------- Utility Functions ---------- */

char *lnget() {
	static char buf[BUFFER_SIZE];
	ln++;
	return fgets(buf, sizeof(buf), stdin);
}

int sql_mac(const char *macro, char *line) {
	if (*line++ != cc) return 0;
	while (*line == ' ' || *line == '\t') line++;

	char word[BUFFER_SIZE] = {0};
	sscanf(line, "%s", word);
	return strcmp(word, macro) == 0;
}

int sql_arg(char *line, char *arg, int len) {
	if (*line++ != cc) return 1;
	while (*line && (*line == ' ' || *line == '\t')) line++;
	while (*line && *line != ' ' && *line != '\t') line++;
	while (*line && (*line == ' ' || *line == '\t')) line++;
	sscanf(line, "%s", arg);
	return 0;
}

char *read_query_until_end() {
	char *query = malloc(BUFFER_SIZE);
	if (!query) return NULL;
	int bufsize = BUFFER_SIZE, querylen = 0;

	char *line;
	while ((line = lnget())) {
		if (sql_mac("sqlend", line)) break;
		int len = strlen(line);
		while (querylen + len + 1 > bufsize) {
			bufsize += BUFFER_SIZE;
			char *temp = realloc(query, bufsize);
			if (!temp) {
				free(query);
				return NULL;
			}
			query = temp;
		}
		memcpy(query + querylen, line, len);
		querylen += len;
	}
	query[querylen] = '\0';
	return query;
}

/* ---------- Query Execution ---------- */

int sql_exec(char *query, int (*callback)()) {
	char *errmsg = NULL;
	int rc = sqlite3_exec(db, query, callback, 0, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Line %d: SQL error: %s\nQuery: %s\n", ln, errmsg, query);
		sqlite3_free(errmsg);
		return 1;
	}
	return 0;
}

int sql_query(int (*callback)()) {
	if (!sqlopened) {
		fprintf(stderr, "Line %d: No database opened.\n", ln);
		return 1;
	}
	char *query = read_query_until_end();
	if (!query) {
		fprintf(stderr, "Line %d: Failed to allocate memory for query.\n", ln);
		return 1;
	}
	int result = sql_exec(query, callback);
	free(query);
	return result;
}

int sql_file(const char *filename) {
	FILE *file = fopen(filename, "r");
	if (!file) {
		fprintf(stderr, "Line %d: Could not open file %s\n", ln, filename);
		return 1;
	}

	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	rewind(file);

	char *query = malloc(size + 1);
	if (!query) return fclose(file), 1;

	fread(query, 1, size, file);
	query[size] = '\0';
	fclose(file);

	int result = sql_exec(query, sql_beg);
	free(query);
	return result;
}

/* ---------- Main Parser ---------- */

int sql() {
	char *line;
	char arg[BUFFER_SIZE];

	while ((line = lnget())) {
		if (sql_mac("sqldb", line)) {
			if (sql_arg(line, arg, BUFFER_SIZE)) {
				fprintf(stderr, "Line %d: Missing database name.\n", ln);
				return 1;
			}
			if (sqlopened) sql_close();
			sqlopened = 1;
			if (sql_open(arg)) return 1;
			continue;
		}

		if (sql_mac("sqlds", line)) {
			if (sql_arg(line, arg, BUFFER_SIZE)) {
				fprintf(stderr, "Line %d: Missing argument.\n", ln);
				return 1;
			}
			printf("%cds %s", cc, arg);
			if (sql_query(sql_str)) return 1;
			continue;
		}

		if (sql_mac("sqlnr", line)) {
			if (sql_arg(line, arg, BUFFER_SIZE)) {
				fprintf(stderr, "Line %d: Missing argument.\n", ln);
				return 1;
			}
			printf("%cnr %s", cc, arg);
			if (sql_query(sql_str)) return 1;
			continue;
		}

		if (sql_mac("sqltbl", line)) {
			if (!sql_arg(line, arg, BUFFER_SIZE) && arg[0] != '\0')
				sqltab = arg[0];
			if (sql_query(sql_tbl)) return 1;
			continue;
		}

		if (sql_mac("sqlbeg", line)) {
			if (sql_query(sql_beg)) return 1;
			continue;
		}

		if (sql_mac("sqlso", line)) {
			if (sql_arg(line, arg, BUFFER_SIZE)) {
				fprintf(stderr, "Line %d: Missing filename.\n", ln);
				return 1;
			}
			if (sql_file(arg)) return 1;
			continue;
		}

		/* Default: pass through */
		printf("%s", line);
	}
	sql_close();
	return 0;
}

int main(int argc, char *argv[]) {
	for (int i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && argv[i][1] == 'c') {
			cc = argv[i][2] ? argv[i][2] : argv[++i][0];
		} else {
			fprintf(stderr,
				"Usage: neatrefer [options] <input >output\n"
				"Options:\n"
				"\t-c    specify control character (default '.')\n");
			return 1;
		}
	}
	return sql();
}
