/*
** libzbxpgsql - A PostgreSQL monitoring module for Zabbix
** Copyright (C) 2015 - Ryan Armstrong <ryan@cavaliercoder.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

/*
 * See:
 *     LibPQ:       http://www.postgresql.org/docs/9.4/static/libpq.html
 *     Statistics:  http://www.postgresql.org/docs/9.4/static/monitoring-stats.html
 */

#include "libzbxpgsql.h"

// Global Variables
const char  pgquerypath[MAX_GLOBBING_PATH_LENGTH] = DEFAULT_PG_QUERY_CONF_PATH;
char        *configPath[MAX_NUMBER_CONFIG_FILES];
int         fileCount = 0;
char        *SQLkey[MAX_NUMBER_SQL_STATEMENT_IN_RAM+1];
char        *SQLstmt[MAX_NUMBER_SQL_STATEMENT_IN_RAM+1];
int         SQLcount = 0;

// Forward function definitions
const char * getPGQUERYPATH();
int globerror(const char *filename, int errorcode);
int SQLCleanup();
int globfilelist(const char *pattern);
int readconfig(const char *cfgfile);

int init_config() {
    char        confdir[MAX_GLOBBING_PATH_LENGTH];
    int         numfiles, i;
    glob_t      filenames;

    // init arrays
    SQLkey[0]=NULL;
    SQLstmt[0]=NULL;

    // set config directory path
    zbx_strlcpy(confdir, getPGQUERYPATH(), strlen(getPGQUERYPATH())+1);

    // see if config dir exists
    switch (glob(confdir, GLOB_ERR, globerror, &filenames)) {
        case 0 :
            globfree(&filenames);
            break;
        case GLOB_NOMATCH :
            zabbix_log(LOG_LEVEL_INFORMATION, "Config dir \"%s\" not found, continuing.", getPGQUERYPATH());
            fileCount = 0;
            return ZBX_MODULE_OK;
        default :
            zabbix_log(LOG_LEVEL_CRIT, "ERROR invoking globfilelist function");
            return ZBX_MODULE_FAIL;
    }

    // append slash if needed, plus *.conf glob
    if ('/' == confdir[strlen(confdir)-1]) {
        strcat(confdir,"*.conf");
    } else {
        strcat(confdir,"/*.conf");
    }

    // get the number of files that matched the glob
    numfiles = globfilelist(confdir);

    // process all the config files
    if (numfiles < 0) {
        zabbix_log(LOG_LEVEL_CRIT, "ERROR invoking globfilelist function");
        return ZBX_MODULE_FAIL;
    }
    for (i = 0; i < numfiles; i++) {
        zabbix_log(LOG_LEVEL_INFORMATION, "Parsing config file \"%s\"", configPath[i]);
        if(readconfig(configPath[i]) == EXIT_FAILURE) {
            zbx_free(configPath[i]);
            return ZBX_MODULE_FAIL;
        }
        zbx_free(configPath[i]);
    }

    return ZBX_MODULE_OK;
}
/*
 * Function getPGQUERYPATH
 *
 * Returns the config directory used for SQL config files
 * for pg_query.* keys.
 * 
 * If the environment variable PGQUERYPATH is set then that is
 * used, otherwise DEFAULT_PG_QUERY_CONF_PATH is used.
 *
 * Returns: pointer to const char
 *
 */
const char * getPGQUERYPATH() {
    const char  *__function_name = "getPGQUERYPATH";
    const char  *envPGQUERYPATH = getenv("PGQUERYPATH");

    zabbix_log(LOG_LEVEL_DEBUG, "In %s", __function_name);
    if('\0' == envPGQUERYPATH) {
        zabbix_log(LOG_LEVEL_DEBUG, "Using default config path");
        return pgquerypath;
    } else {
        if (strlen(envPGQUERYPATH) > MAX_GLOBBING_PATH_LENGTH) {
            zabbix_log(LOG_LEVEL_ERR, "ERROR: Env variable \"PGQUERYPATH\" value too long");
            zabbix_log(LOG_LEVEL_ERR, "ERROR: Length %i exceeds max length of %i",
                strlen(envPGQUERYPATH), MAX_GLOBBING_PATH_LENGTH);
            return NULL;
        }
        zabbix_log(LOG_LEVEL_DEBUG, "Using config path from PGQUERYPATH env variable");
        return envPGQUERYPATH;
    }
}

/*
 * Function globerror
 *
 * Error handler for globbing.
 *
 */
int globerror(const char *filename, int errorcode) {
    zabbix_log(LOG_LEVEL_ERR, "ERROR: globbing error %s: %s", filename, strerror(errorcode));
    return EXIT_FAILURE;
}

/*
 * Function globfilelist
 *
 * Takes a file glob pattern and stores matching
 * filenames in configPath array.
 *
 * Returns:
 *    int number of files matched
 *    -1 if error encountered
 */
int globfilelist(const char *pattern) {
    const char  *__function_name = "globfilelist";
    glob_t   filenames;
    int      strsize;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s", __function_name);
    zabbix_log(LOG_LEVEL_INFORMATION, "Looking for config files: \"%s\"", pattern);
    switch(glob(pattern, GLOB_ERR, globerror, &filenames)) {
        case 0 :
            for (fileCount = 0; fileCount < filenames.gl_pathc; fileCount++) {
                if (fileCount >= MAX_NUMBER_CONFIG_FILES) {
                    zabbix_log(LOG_LEVEL_ERR, "ERROR: Found more than %i config files: %i", MAX_NUMBER_CONFIG_FILES, fileCount);
                    return EXIT_FAILURE;
                }
                strsize = strlen(filenames.gl_pathv[fileCount]) + 1;
                configPath[fileCount] = zbx_malloc(configPath[fileCount],strsize * sizeof(char));
                if (configPath[fileCount] == NULL) {
                    zabbix_log(LOG_LEVEL_CRIT, "ERROR: zbx_malloc failed");
                    return EXIT_FAILURE;
                }
                zbx_strlcpy(configPath[fileCount],filenames.gl_pathv[fileCount],strlen(filenames.gl_pathv[fileCount])+1);
            }
            globfree(&filenames);
            break;
        case GLOB_NOMATCH :
            zabbix_log(LOG_LEVEL_INFORMATION, "No config files found to process... skipping");
            fileCount = 0;
            break;
        default : // untrapped glob error
            fileCount = -1;
    }
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s (fileCount=%i)", __function_name, fileCount);
    return fileCount;
}

/*
 * Function storeSQLstmt
 *
 * A simple key/value store using arrays of pointers.
 *
 * Stores a string key and a string value (SQL statement)
 * into a sorted array, maintaining sort order.
 *
 * Since it's just moving pointers it should perform well
 * even with large numbers of keys.
 *
 * Returns: 
 *   EXIT_SUCCESS = successfully inserted
 *   EXIT_FAILURE = insert failed
 *   -1           = duplicate key discarded
 */
int  storeSQLstmt(const char *key, const char *stmt) {
    const char  *__function_name = "storeSQLstmt";
    int  i;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s(%s,<stmt>)", __function_name, key);
    // make sure we have space
    if (SQLcount >= MAX_NUMBER_SQL_STATEMENT_IN_RAM) {
        zabbix_log(LOG_LEVEL_ERR, "ERROR: Keystore full: %i statements stored already", SQLcount);
        return EXIT_FAILURE;
    }
    // exclude dupes
    if (SQLkeysearch((char *)key) != -1) {
        zabbix_log(LOG_LEVEL_ERR, "ERROR: Duplicate key: \"%s\"", key);
        return EXIT_FAILURE;
    }
    // start at the end of the index and push out
    // entries to the next spot until you find the
    // right spot to insert the new key/value pair
    i = SQLcount - 1;
    zabbix_log(LOG_LEVEL_DEBUG, "Starting to look for insert location (i:%i)", i);
    while (i >= 0 && strcmp(key,SQLkey[i]) < 0) {
        zabbix_log(LOG_LEVEL_DEBUG, "moving data from slot %i to slot %i", i, i+1);
        SQLkey[i+1]  = SQLkey[i];
        SQLstmt[i+1] = SQLstmt[i];
        SQLkey[i] = NULL;
        SQLstmt[i] = NULL;
        i--;
    }
    // allocate memory for the new key
    SQLkey[i+1] = zbx_malloc(SQLkey[i+1],sizeof(char) * (strlen(key)+1));
    if (SQLkey[i+1] == NULL) {
        zabbix_log(LOG_LEVEL_CRIT, "ERROR: zbx_malloc failed");
        return EXIT_FAILURE;
    }
    // allocate memory for the new value
    SQLstmt[i+1] = zbx_malloc(SQLstmt[i+1],sizeof(char) * (strlen(stmt)+1));
    if (SQLstmt[i+1] == NULL) {
        zabbix_log(LOG_LEVEL_CRIT, "ERROR: zbx_malloc failed");
        return EXIT_FAILURE;
    }
    // store the key and value
    zabbix_log(LOG_LEVEL_DEBUG, "storing data in slot %i", i+1);
    zbx_strlcpy(SQLkey[i+1],key,strlen(key)+1);
    zbx_strlcpy(SQLstmt[i+1],stmt,strlen(stmt)+1);
    SQLcount++;

    // Append NULL marker
    SQLkey[SQLcount+1] = NULL;
    SQLstmt[SQLcount+1] = NULL;

    zabbix_log(LOG_LEVEL_DEBUG, "End of %s", __function_name);
    return EXIT_SUCCESS;
}

/*
 * Function SQLkeysearch
 *
 * Searches the key array to find the
 * corresponding SQL stmt using binary
 * search.
 *
 * Returns: 
 *    If Key Found: index to key
 *    If Not Found: -1
 */
int SQLkeysearch(char *key) {
    const char  *__function_name = "SQLkeysearch";
    int  top;
    int  mid;
    int  bottom;
    int  cmp;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s(%s)", __function_name, key);
    top = SQLcount - 1;
    bottom = 0;
    while (bottom <= top) {
        mid = (bottom + top)/2;
        zabbix_log(LOG_LEVEL_DEBUG, "range top:%i mid:%i bottom:%i", top, mid, bottom);
        cmp=strcmp(SQLkey[mid], key);
        if (cmp == 0) {
            zabbix_log(LOG_LEVEL_DEBUG, "found in slot:%i", mid);
            return mid;
        } else if (cmp > 0) {
            zabbix_log(LOG_LEVEL_DEBUG, "key between bottom and middle of range");
            top    = mid - 1;
        } else if (cmp < 0) {
            zabbix_log(LOG_LEVEL_DEBUG, "key between middle and top of range");
            bottom = mid + 1;
        }
    }
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s", __function_name);
    return -1;
}

/*
 * Function SQLcleanup
 *
 * Free up array memory when done.
 *
 */
int  SQLCleanup() {
    const char  *__function_name = "SQLCleanup";
    int   i;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s", __function_name);
    zabbix_log(LOG_LEVEL_DEBUG, "SQLcount:%i", SQLcount);
    for (i = 0; i < SQLcount; i++) {
        zabbix_log(LOG_LEVEL_DEBUG, "i:%i", i);
        zbx_free(SQLkey[i]);
        zbx_free(SQLstmt[i]);
    }
    SQLcount = 0;
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s", __function_name);
    return EXIT_SUCCESS;
}

/*
 * Function readconfig
 *
 * Reads the contents of a config file using libconfig.
 * 
 * Note: although libconfig supports complex parameter
 * files (such as nested parms), this function's
 * implementation only supports simple
 *    key = value
 * parameters.
 *
 * Returns:
 *   EXIT_SUCCESS = successfully parsed
 *   EXIT_FAILURE = parse failed
 */
int readconfig(const char *cfgfile) {
    const char  *__function_name = "readconfig";
    config_t          cfg;
    config_setting_t  *root, *element;
    int               i, rc;
    const char        *key, *value;

    zabbix_log(LOG_LEVEL_DEBUG, "In %s(%s)", __function_name, cfgfile);
    config_init(&cfg);
    // call libconfig to parse config file into memory
    rc = config_read_file(&cfg, cfgfile);
    if (rc != 1) {
        zabbix_log(LOG_LEVEL_ERR, "ERROR: %s for file \"%s\" rc=%i",
            config_error_text(&cfg), cfgfile, rc);
        if (config_error_line(&cfg) != 0) {
            zabbix_log(LOG_LEVEL_ERR, "ERROR: Parsing error on or before line %i",
                config_error_line(&cfg));
        }
        config_destroy(&cfg);
        return EXIT_FAILURE;
    }
    // start retrieving key/value pairs
    root = config_root_setting(&cfg);
    zabbix_log(LOG_LEVEL_DEBUG, "config_setting_length:%i", config_setting_length(root));
    for (i = 0; i < config_setting_length(root); i++) {
        element = config_setting_get_elem(root, i);
        key = config_setting_name(element);
        zabbix_log(LOG_LEVEL_DEBUG, "Found config key=[%s]", key);
        // we only want strings
        if(CONFIG_TYPE_STRING == config_setting_type(element)) {
            value = config_setting_get_string_elem(root, i);
            zabbix_log(LOG_LEVEL_DEBUG, "Found config value=[%s]", value);
            zabbix_log(LOG_LEVEL_INFORMATION, "    Storing key \"%s\" and value", key);
            // store it in our key/value store
            if (storeSQLstmt(key, value) == EXIT_FAILURE) {
                config_destroy(&cfg);
                return EXIT_FAILURE;
            }
        } else {
            zabbix_log(LOG_LEVEL_DEBUG, "config_setting_type:%i", config_setting_type(element));
            zabbix_log(LOG_LEVEL_ERR, "ERROR: Value for key \"%s\" in \"%s\" on line %i is not a string",
                   key, cfgfile, config_setting_source_line(element));
            config_destroy(&cfg);
            return EXIT_FAILURE;
        }
    }
    config_destroy(&cfg);
    zabbix_log(LOG_LEVEL_DEBUG, "End of %s", __function_name);
    return EXIT_SUCCESS;
}
