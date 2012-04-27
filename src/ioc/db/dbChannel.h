/*************************************************************************\
* Copyright (c) 2010 UChicago Argonne LLC, as Operator of Argonne
 *     National Laboratory.
 * EPICS BASE is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 \*************************************************************************/

#ifndef INC_dbChannel_H
#define INC_dbChannel_H

#include "dbDefs.h"
#include "dbAddr.h"
#include "ellLib.h"
#include "epicsTypes.h"
#include "errMdef.h"
#include "shareLib.h"

#ifdef __cplusplus
// extern "C" {
#endif

#define DBCHANNEL_MAGIC 0xdbc4a9e1

#define S_db_notInit  (M_dbAccess|21) /*dbChannel not initialized*/

/* A dbChannel points to a record field, and can have multiple filters */
typedef struct dbChannel {
    epicsUInt32 magic;
    dbAddr addr;
    ELLLIST filters;
} dbChannel;

typedef struct chFilter chFilter;

/* Return values from chFilterIf->parse_* routines: */
typedef enum {
    parse_stop, parse_continue
} parse_result;

/* These routines must be implemented by each filter plug-in */
typedef struct chFilterIf {
    /* Parsing event handlers: */
    parse_result (* parse_start)(chFilter *filter);
    /* If parse_start() returns parse_continue for a filter, one of
     * parse_abort() or parse_end() will later be called for that same
     * filter.
     */
    void (* parse_abort)(chFilter *filter);
    /* If parse_abort() is called it should release any memory allocated
     * for this filter; no further parse_...() calls will be made;
     */
    parse_result (* parse_end)(chFilter *filter);
    /* If parse_end() returns parse_stop it should have released any
     * memory allocated for this filter; no further parse_...() calls will
     * be made in this case.
     */

    parse_result (* parse_null)(chFilter *filter);
    parse_result (* parse_boolean)(chFilter *filter, int boolVal);
    parse_result (* parse_integer)(chFilter *filter, long integerVal);
    parse_result (* parse_double)(chFilter *filter, double doubleVal);
    parse_result (* parse_string)(chFilter *filter, const char *stringVal,
            size_t stringLen); /* NB: stringVal is not zero-terminated: */

    parse_result (* parse_start_map)(chFilter *filter);
    parse_result (* parse_map_key)(chFilter *filter, const char *key,
            size_t stringLen); /* NB: key is not zero-terminated: */
    parse_result (* parse_end_map)(chFilter *filter);

    parse_result (* parse_start_array)(chFilter *filter);
    parse_result (* parse_end_array)(chFilter *filter);

    /* Channel operations: */
    long (* channel_open)(chFilter *filter);
    void (* channel_report)(chFilter *filter, int level);
    /* FIXME: More routines here ... */
    void (* channel_close)(chFilter *filter);
} chFilterIf;

/* A chFilter holds instance data for a single filter */
struct chFilter {
    ELLNODE node;
    dbChannel *chan;
    const chFilterIf *fif;
    void *puser;
};

epicsShareFunc long dbChannelFind(dbChannel *chan, const char *pname);
epicsShareFunc long dbChannelOpen(dbChannel *chan);
epicsShareFunc void dbChannelReport(dbChannel *chan, int level);
epicsShareFunc long dbChannelClose(dbChannel *chan);

epicsShareFunc void dbRegisterFilter(const char *key, const chFilterIf *fif);
epicsShareFunc const chFilterIf * dbFindFilter(const char *key, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* INC_dbChannel_H */
