/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2008 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.01 of the PHP license,      |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_01.txt                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Antony Dovgal <tony at daylessday.org>                       |
  | Based on Sphinx PHP API by Andrew Aksyonoff <shodan at shodan.ru>    |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "php.h"
#include "php_ini.h"
#include "ext/standard/info.h"
#include "ext/standard/file.h"
#include "zend_operators.h"
#include "php_sphinx.h"

#include <sphinxclient.h>

static zend_class_entry *ce_sphinx_client;

static zend_object_handlers php_sphinx_client_handlers;
static zend_object_handlers cannot_be_cloned;

typedef struct _php_sphinx_client {
	sphinx_client *sphinx;
	zend_bool array_result;
	zend_object std;
} php_sphinx_client;

#ifdef COMPILE_DL_SPHINX
ZEND_GET_MODULE(sphinx)
#endif

#ifndef E_RECOVERABLE_ERROR
#define E_RECOVERABLE_ERROR E_WARNING
#endif

#define SPHINX_CONST(name) REGISTER_LONG_CONSTANT(#name, name, CONST_CS | CONST_PERSISTENT)

#define SPHINX_INITIALIZED(c) \
		if (!(c) || !(c)->sphinx) { \
			php_error_docref(NULL, E_WARNING, "using uninitialized SphinxClient object"); \
			RETURN_FALSE; \
		}

static inline php_sphinx_client *php_sphinx_client_object(zend_object *obj) {
	return (php_sphinx_client *)((char*)(obj) - XtOffsetOf(php_sphinx_client, std));
}

#define sphinx_client(zv) php_sphinx_client_object(Z_OBJ_P(zv))

static void php_sphinx_client_obj_dtor(zend_object *object) /* {{{ */
{
	php_sphinx_client *c = php_sphinx_client_object(object);

	sphinx_destroy(c->sphinx);
	zend_object_std_dtor(&c->std);
}
/* }}} */

static zend_object *php_sphinx_client_new(zend_class_entry *ce) /* {{{ */
{
	php_sphinx_client *c;

	c = ecalloc(1, sizeof(*c) + zend_object_properties_size(ce));
	zend_object_std_init(&c->std, ce);
	object_properties_init(&c->std, ce);
	c->std.handlers = &php_sphinx_client_handlers;
	return &c->std;
}
/* }}} */

static zval *php_sphinx_client_read_property(zval *object, zval *member, int type, void **cache_slot, zval *rv) /* {{{ */
{
	//php_sphinx_client *c;
	zval tmp_member;
	zval *retval;
	zend_object_handlers *std_hnd;

	//c = sphinx_client(object);

	if (Z_TYPE_P(member) != IS_STRING) {
		tmp_member = *member;
		zval_copy_ctor(&tmp_member);
		convert_to_string(&tmp_member);
		member = &tmp_member;
	}

	/* XXX we can either create retval ourselves (for custom properties) or use standard handlers */

	std_hnd = zend_get_std_object_handlers();
	retval = std_hnd->read_property(object, member, type, cache_slot, rv);

	if (member == &tmp_member) {
		zval_dtor(member);
	}
	return retval;
}
/* }}} */

static HashTable *php_sphinx_client_get_properties(zval *object) /* {{{ */
{
	php_sphinx_client *c;
	const char *warning, *error;
	zval tmp;
	HashTable *props;

	c = sphinx_client(object);
	props = zend_std_get_properties(object);

	error = sphinx_error(c->sphinx);
	ZVAL_STRING(&tmp, (char *)error);
	zend_hash_str_update(props, "error", strlen("error"), &tmp);

	warning = sphinx_warning(c->sphinx);
	ZVAL_STRING(&tmp, (char *)warning);
	zend_hash_str_update(props, "warning", strlen("warning"), &tmp);
	return c->std.properties;
}
/* }}} */

static void php_sphinx_result_to_array(php_sphinx_client *c, sphinx_result *result, zval *array) /* {{{ */
{
	zval tmp;
	int i, j;

	array_init(array);

	/* error */
	if (!result->error) {
		add_assoc_string(array, "error", "");
	} else {
		add_assoc_string(array, "error", (char *)(result->error));
	}

	/* warning */
	if (!result->warning) {
		add_assoc_string(array, "warning", "");
	} else {
		add_assoc_string(array, "warning", (char *)result->warning);
	}

	/* status */
	add_assoc_long(array, "status", result->status);

	switch(result->status) {
		case SEARCHD_OK:
			/* ok, continue reading data */
			break;
		case SEARCHD_WARNING:
			/* this seems to be safe, too */
			break;
		default:
			/* libsphinxclient doesn't nullify the data
			   in case of error, so it's not safe to continue. */
			return;
	}

	/* fields */
	array_init(&tmp);

	for (i = 0; i < result->num_fields; i++) {
		add_next_index_string(&tmp, result->fields[i]);
	}
	add_assoc_zval(array, "fields", &tmp);

	/* attrs */
	array_init(&tmp);

	for (i = 0; i < result->num_attrs; i++) {
#if SIZEOF_LONG == 8
		add_assoc_long(&tmp, result->attr_names[i], result->attr_types[i]);
#else
		double float_value;
		char buf[128];

		float_value = (double)result->attr_types[i];
		slprintf(buf, sizeof(buf), "%.0f", float_value);
		add_assoc_string(&tmp, result->attr_names[i], buf);
#endif
	}
	add_assoc_zval(array, "attrs", &tmp);

	/* matches */
	if (result->num_matches) {
		array_init(&tmp);

		for (i = 0; i < result->num_matches; i++) {
			zval tmp_element, sub_element;

			array_init(&tmp_element);

			if (c->array_result) {
				/* id */
#if SIZEOF_LONG == 8
				add_assoc_long(&tmp_element, "id", sphinx_get_id(result, i));
#else
				double float_id;
				char buf[128];

				float_id = (double)sphinx_get_id(result, i);
				slprintf(buf, sizeof(buf), "%.0f", float_id);
				add_assoc_string(&tmp_element, "id", buf);
#endif
			}

			/* weight */
			add_assoc_long(&tmp_element, "weight", sphinx_get_weight(result, i));

			/* attrs */
			array_init(&sub_element);

			for (j = 0; j < result->num_attrs; j++) {
				zval sub_sub_element;
#if SIZEOF_LONG != 8
				double float_value;
				char buf[128];
#endif

				switch(result->attr_types[j]) {
					case SPH_ATTR_MULTI | SPH_ATTR_INTEGER:
						{
							int k;
							unsigned int *mva = sphinx_get_mva(result, i, j);
							unsigned int tmp, num;

							array_init(&sub_sub_element);

							if (!mva) {
								break;
							}

							memcpy(&num, mva, sizeof(unsigned int));

							for (k = 1; k <= num; k++) {
								mva++;
								memcpy(&tmp, mva, sizeof(unsigned int));
#if SIZEOF_LONG == 8
								add_next_index_long(&sub_sub_element, tmp);
#else
								float_value = (double)tmp;
								slprintf(buf, sizeof(buf), "%.0f", float_value);
								add_next_index_string(&sub_sub_element, buf);
#endif
							}
						}	break;

					case SPH_ATTR_FLOAT:
						ZVAL_DOUBLE(&sub_sub_element, sphinx_get_float(result, i, j));
						break;
#if LIBSPHINX_VERSION_ID >= 110
					case SPH_ATTR_STRING:
						ZVAL_STRING(&sub_sub_element, sphinx_get_string(result, i, j));
						break;
#endif
					default:
#if SIZEOF_LONG == 8
						ZVAL_LONG(&sub_sub_element, sphinx_get_int(result, i, j));
#else
						float_value = (double)sphinx_get_int(result, i, j);
						slprintf(buf, sizeof(buf), "%.0f", float_value);
						ZVAL_STRING(&sub_sub_element, buf);
#endif
						break;
				}

				add_assoc_zval(&sub_element, result->attr_names[j], &sub_sub_element);
			}

			add_assoc_zval(&tmp_element, "attrs", &sub_element);

			if (c->array_result) {
				add_next_index_zval(&tmp, &tmp_element);
			} else {
#if SIZEOF_LONG == 8
				add_index_zval(&tmp, sphinx_get_id(result, i), &tmp_element);
#else
				char buf[128];
				double float_id;
				int buf_len;

				float_id = (double)sphinx_get_id(result, i);
				buf_len = slprintf(buf, sizeof(buf), "%.0f", float_id);
				add_assoc_zval(tmp, buf, &tmp_element);
#endif
			}
		}

		add_assoc_zval(array, "matches", &tmp);
	}

	/* total */
	add_assoc_long(array, "total", result->total);

	/* total_found */
	add_assoc_long(array, "total_found", result->total_found);

	/* time */
	add_assoc_double(array, "time", (double)result->time_msec/1000.0);

	/* words */
	if (result->num_words) {
		zval tmp;

		array_init(&tmp);
		for (i = 0; i < result->num_words; i++) {
			zval sub_element;

			array_init(&sub_element);

			add_assoc_long(&sub_element, "docs", result->words[i].docs);
			add_assoc_long(&sub_element, "hits", result->words[i].hits);
			add_assoc_zval(&tmp, (char *)result->words[i].word, &sub_element);
		}
		add_assoc_zval(array, "words", &tmp);
	}
}
/* }}} */


/* {{{ proto void SphinxClient::__construct() */
static PHP_METHOD(SphinxClient, __construct)
{
	php_sphinx_client *c;

	c = sphinx_client(getThis());

	if (c->sphinx) {
		/* called __construct() twice, bail out */
		return;
	}

	c->sphinx = sphinx_create(1 /* copy string args */);

	sphinx_set_connect_timeout(c->sphinx, FG(default_socket_timeout));
}
/* }}} */

/* {{{ proto bool SphinxClient::setServer(string server, int port) */
static PHP_METHOD(SphinxClient, setServer)
{
	php_sphinx_client *c;
	zend_long port;
	char *server;
	int res;
	size_t server_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl", &server, &server_len, &port) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_server(c->sphinx, server, (int)port);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setLimits(int offset, int limit[, int max_matches[, int cutoff]]) */
static PHP_METHOD(SphinxClient, setLimits)
{
	php_sphinx_client *c;
	zend_long offset, limit, max_matches = 1000, cutoff = 0;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ll|ll", &offset, &limit, &max_matches, &cutoff) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_limits(c->sphinx, (int)offset, (int)limit, (int)max_matches, (int)cutoff);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setMatchMode(int mode) */
static PHP_METHOD(SphinxClient, setMatchMode)
{
	php_sphinx_client *c;
	zend_long mode;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &mode) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_match_mode(c->sphinx, mode);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setIndexWeights(array weights) */
static PHP_METHOD(SphinxClient, setIndexWeights)
{
	php_sphinx_client *c;
	zval *weights, *item;
	int num_weights, res = 0, i;
	int *index_weights;
	char **index_names;
	zend_string *string_key;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "a", &weights) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	num_weights = zend_hash_num_elements(Z_ARRVAL_P(weights));
	if (!num_weights) {
		/* check for empty array and return false right away */
		RETURN_FALSE;
	}

	index_names = safe_emalloc(num_weights, sizeof(char *), 0);
	index_weights = safe_emalloc(num_weights, sizeof(int), 0);

	/* reset num_weights, we'll reuse it count _real_ number of entries */
	num_weights = 0;

	ZEND_HASH_FOREACH_STR_KEY_VAL_IND(Z_ARRVAL_P(weights), string_key, item) {
		if (!string_key) {
			/* if the key is not string.. well.. you're screwed */
			break;
		}

		index_names[num_weights] = estrndup(string_key->val, string_key->len);
		index_weights[num_weights] = zval_get_long(item);

		num_weights++;
	} ZEND_HASH_FOREACH_END();

	if (num_weights) {
		res = sphinx_set_index_weights(c->sphinx, num_weights, (const char **)index_names, index_weights);
	}

	for (i = 0; i != num_weights; i++) {
		efree(index_names[i]);
	}
	efree(index_names);
	efree(index_weights);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

#if LIBSPHINX_VERSION_ID >= 99
/* {{{ proto bool SphinxClient::setSelect(string clause) */
static PHP_METHOD(SphinxClient, setSelect)
{
	php_sphinx_client *c;
	char *clause;
	int res;
	size_t clause_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &clause, &clause_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_select(c->sphinx, clause);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */
#endif

/* {{{ proto bool SphinxClient::setIDRange(int min, int max) */
static PHP_METHOD(SphinxClient, setIDRange)
{
	php_sphinx_client *c;
	zend_long min, max;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ll", &min, &max) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_id_range(c->sphinx, (sphinx_uint64_t)min, (sphinx_uint64_t)max);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setFilter(string attribute, array values[, bool exclude]) */
static PHP_METHOD(SphinxClient, setFilter)
{
	php_sphinx_client *c;
	zval *values, *item;
	char *attribute;
	int	num_values, i = 0, res;
	zend_bool exclude = 0;
	sphinx_int64_t *u_values;
	size_t attribute_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sa|b", &attribute, &attribute_len, &values, &exclude) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	num_values = zend_hash_num_elements(Z_ARRVAL_P(values));
	if (!num_values) {
		RETURN_FALSE;
	}

	u_values = safe_emalloc(num_values, sizeof(sphinx_int64_t), 0);

	ZEND_HASH_FOREACH_VAL_IND(Z_ARRVAL_P(values), item) {
		u_values[i] = (sphinx_int64_t)zval_get_double(item);
		i++;
	} ZEND_HASH_FOREACH_END();

	res = sphinx_add_filter(c->sphinx, attribute, num_values, u_values, exclude ? 1 : 0);
	efree(u_values);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

#ifdef HAVE_SPHINX_ADD_FILTER_STRING
/* {{{ proto bool SphinxClient::setFilterString(string attribute, string value[, bool exclude]) */
static PHP_METHOD(SphinxClient, setFilterString)
{
	php_sphinx_client *c;
	char *attribute, *value;
	int	res;
	zend_bool exclude = 0;
	size_t attribute_len, value_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ss|b", &attribute, &attribute_len, &value, &value_len, &exclude) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_add_filter_string(c->sphinx, attribute, value, exclude ? 1 : 0);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */
#endif

/* {{{ proto bool SphinxClient::setFilterRange(string attribute, int min, int max[, bool exclude]) */
static PHP_METHOD(SphinxClient, setFilterRange)
{
	php_sphinx_client *c;
	char *attribute;
	int res;
	zend_long min, max;
	zend_bool exclude = 0;
	size_t attribute_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sll|b", &attribute, &attribute_len, &min, &max, &exclude ) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_add_filter_range(c->sphinx, attribute, min, max, exclude);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setFilterFloatRange(string attribute, float min, float max[, bool exclude]) */
static PHP_METHOD(SphinxClient, setFilterFloatRange)
{
	php_sphinx_client *c;
	char *attribute;
	int res;
	double min, max;
	zend_bool exclude = 0;
	size_t attribute_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sdd|b", &attribute, &attribute_len, &min, &max, &exclude) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_add_filter_float_range(c->sphinx, attribute, min, max, exclude);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setGeoAnchor(string attrlat, string attrlong, float latitude, float longitude) */
static PHP_METHOD(SphinxClient, setGeoAnchor)
{
	php_sphinx_client *c;
	char *attrlat, *attrlong;
	int res;
	double latitude, longitude;
	size_t attrlat_len, attrlong_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ssdd", &attrlat, &attrlat_len, &attrlong, &attrlong_len, &latitude, &longitude) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_geoanchor(c->sphinx, attrlat, attrlong, latitude, longitude);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setGroupBy(string attribute, int func[, string groupsort]) */
static PHP_METHOD(SphinxClient, setGroupBy)
{
	php_sphinx_client *c;
	char *attribute, *groupsort = NULL;
	int res;
	zend_long func;
	size_t attribute_len, groupsort_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sl|s", &attribute, &attribute_len, &func, &groupsort, &groupsort_len) == FAILURE) {
		return;
	}

	if (groupsort == NULL) {
		groupsort = "@group desc";
	}

	if (func < SPH_GROUPBY_DAY || func > SPH_GROUPBY_ATTRPAIR) {
		php_error_docref(NULL, E_WARNING, "invalid group func specified (%ld)", func);
		RETURN_FALSE;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_groupby(c->sphinx, attribute, func, groupsort);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setGroupDistinct(string attribute) */
static PHP_METHOD(SphinxClient, setGroupDistinct)
{
	php_sphinx_client *c;
	char *attribute;
	int res;
	size_t attribute_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &attribute, &attribute_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_groupby_distinct(c->sphinx, attribute);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setRetries(int count[, int delay]) */
static PHP_METHOD(SphinxClient, setRetries)
{
	php_sphinx_client *c;
	zend_long count, delay = 0;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l|l", &count, &delay) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_retries(c->sphinx, (int)count, (int)delay);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setMaxQueryTime(int qtime) */
static PHP_METHOD(SphinxClient, setMaxQueryTime)
{
	php_sphinx_client *c;
	zend_long qtime;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &qtime) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_max_query_time(c->sphinx, (int)qtime);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

#ifdef HAVE_3ARG_SPHINX_SET_RANKING_MODE
/* {{{ proto bool SphinxClient::setRankingMode(int ranker[, string ranking_expression]) */
static PHP_METHOD(SphinxClient, setRankingMode)
{
	php_sphinx_client *c;
	zend_long ranker;
	int res;
	char *rank_expr = NULL;
	size_t rank_expr_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l|s", &ranker, &rank_expr, &rank_expr_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_ranking_mode(c->sphinx, (int)ranker, rank_expr);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */
#else
/* {{{ proto bool SphinxClient::setRankingMode(int ranker) */
static PHP_METHOD(SphinxClient, setRankingMode)
{
	php_sphinx_client *c;
	zend_long ranker;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l", &ranker) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_ranking_mode(c->sphinx, (int)ranker);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */
#endif

/* {{{ proto bool SphinxClient::setFieldWeights(array weights) */
static PHP_METHOD(SphinxClient, setFieldWeights)
{
	php_sphinx_client *c;
	zval *weights, *item;
	int num_weights, res = 0, i;
	int *field_weights;
	char **field_names;
	zend_string *string_key;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "a", &weights) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	num_weights = zend_hash_num_elements(Z_ARRVAL_P(weights));
	if (!num_weights) {
		/* check for empty array and return false right away */
		RETURN_FALSE;
	}

	field_names = safe_emalloc(num_weights, sizeof(char *), 0);
	field_weights = safe_emalloc(num_weights, sizeof(int), 0);

	/* reset num_weights, we'll reuse it count _real_ number of entries */
	num_weights = 0;

	ZEND_HASH_FOREACH_STR_KEY_VAL_IND(Z_ARRVAL_P(weights), string_key, item) {

		if (!string_key) {
			/* if the key is not string.. well.. you're screwed */
			break;
		}

		field_names[num_weights] = estrndup(string_key->val, string_key->len);
		field_weights[num_weights] = zval_get_long(item);

		num_weights++;
	} ZEND_HASH_FOREACH_END();

	if (num_weights) {
		res = sphinx_set_field_weights(c->sphinx, num_weights, (const char **) field_names, field_weights);
	}

	for (i = 0; i != num_weights; i++) {
		efree(field_names[i]);
	}
	efree(field_names);
	efree(field_weights);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setSortMode(int mode[, string sortby]) */
static PHP_METHOD(SphinxClient, setSortMode)
{
	php_sphinx_client *c;
	zend_long mode;
	char *sortby = NULL;
	int res;
	size_t sortby_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "l|s", &mode, &sortby, &sortby_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_sort_mode(c->sphinx, (int)mode, sortby);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setConnectTimeout(float timeout) */
static PHP_METHOD(SphinxClient, setConnectTimeout)
{
	php_sphinx_client *c;
	double timeout;
	int res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "d", &timeout) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_set_connect_timeout(c->sphinx, timeout);

	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::setArrayResult(bool array_result) */
static PHP_METHOD(SphinxClient, setArrayResult)
{
	php_sphinx_client *c;
	zend_bool array_result;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "b", &array_result) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	c->array_result = array_result;
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto int SphinxClient::updateAttributes(string index, array attributes, array values[, bool mva]) */
static PHP_METHOD(SphinxClient, updateAttributes)
{
	php_sphinx_client *c;
	zval *attributes, *values, *item;
	char *index;
	const char **attrs;
	int attrs_num, values_num;
	int res = 0;
	sphinx_uint64_t *docids = NULL;
	sphinx_int64_t *vals = NULL;
	unsigned int *vals_mva = NULL;
#if LIBSPHINX_VERSION_ID >= 110
	int res_mva, values_mva_num, values_mva_size = 0;
	zval *attr_value_mva;
#endif
	int a = 0, i = 0, j = 0;
	zend_bool mva = 0;
	size_t index_len;
	ulong id;
	zend_string *str_id;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "saa|b", &index, &index_len, &attributes, &values, &mva) == FAILURE) {
		return;
	}

#if LIBSPHINX_VERSION_ID < 110
	if (mva) {
		php_error_docref(NULL, E_WARNING, "update mva attributes is not supported");
		RETURN_FALSE;
	}
#endif

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	attrs_num = zend_hash_num_elements(Z_ARRVAL_P(attributes));

	if (!attrs_num) {
		php_error_docref(NULL, E_WARNING, "empty attributes array passed");
		RETURN_FALSE;
	}

	values_num = zend_hash_num_elements(Z_ARRVAL_P(values));

	if (!values_num) {
		php_error_docref(NULL, E_WARNING, "empty values array passed");
		RETURN_FALSE;
	}

	attrs = emalloc(sizeof(char *) * attrs_num);
	ZEND_HASH_FOREACH_VAL_IND(Z_ARRVAL_P(attributes), item) {
		if (Z_TYPE_P(item) != IS_STRING) {
			php_error_docref(NULL, E_WARNING, "non-string attributes are not allowed");
			break;
		}
		attrs[a] = Z_STRVAL_P(item); /* no copying here! */
		a++;
	} ZEND_HASH_FOREACH_END();

	/* cleanup on error */
	if (a != attrs_num) {
		RETVAL_FALSE;
		goto cleanup;
	}

	docids = emalloc(sizeof(sphinx_int64_t) * values_num);
	if (!mva) {
		vals = safe_emalloc(values_num * attrs_num, sizeof(sphinx_int64_t), 0);
	}
	ZEND_HASH_FOREACH_KEY_VAL_IND(Z_ARRVAL_P(values), id, str_id, item) {
		zval *attr_value;
		int failed = 0;
		double float_id = 0;
		unsigned char id_type;

		if (Z_TYPE_P(item) != IS_ARRAY) {
			php_error_docref(NULL, E_WARNING, "value is not an array of attributes");
			break;
		}

		if (zend_hash_num_elements(Z_ARRVAL_P(item)) != attrs_num) {
			php_error_docref(NULL, E_WARNING, "number of values is not equal to the number of attributes");
			break;
		}

		if (!str_id) {
			/* ok */
			id_type = IS_LONG;
		} else {
			id_type = is_numeric_string(str_id->val, str_id->len, (long *)&id, &float_id, 0);
			if (id_type == IS_LONG || id_type == IS_DOUBLE) {
				/* ok */
			} else {
				php_error_docref(NULL, E_WARNING, "document ID must be numeric");
				break;
			}
		}

		if (id_type == IS_LONG) {
			docids[i] = (sphinx_uint64_t)id;
		} else { /* IS_FLOAT */
			docids[i] = (sphinx_uint64_t)float_id;
		}

		a = 0;
		ZEND_HASH_FOREACH_VAL_IND(Z_ARRVAL_P(item), attr_value) {
			if (mva) {
#if LIBSPHINX_VERSION_ID >= 110
				if (Z_TYPE_P(attr_value) != IS_ARRAY) {
					php_error_docref(NULL, E_WARNING, "attribute value must be an array");
					failed = 1;
					break;
				}
				values_mva_num = zend_hash_num_elements(Z_ARRVAL_P(attr_value));
				if (values_mva_num > values_mva_size) {
					values_mva_size = values_mva_num;
					vals_mva = safe_erealloc(vals_mva, values_mva_size, sizeof(unsigned int), 0);
				}
				if (vals_mva) {
					memset(vals_mva, 0, values_mva_size * sizeof(unsigned int));
				}

				j = 0;
				ZEND_HASH_FOREACH_VAL_IND(Z_ARRVAL_P(attr_value), attr_value_mva) {
					if (Z_TYPE_P(attr_value_mva) != IS_LONG) {
						php_error_docref(NULL, E_WARNING, "mva attribute value must be integer");
						failed = 1;
						break;
					}
					vals_mva[j] = (unsigned int)Z_LVAL_P(attr_value_mva);
					j++;
				} ZEND_HASH_FOREACH_END();

				if (failed) {
					break;
				}

				res_mva = sphinx_update_attributes_mva(c->sphinx, index, attrs[a], docids[i], values_mva_num, vals_mva);

				if (res_mva < 0) {
					failed = 1;
					break;
				}
#endif
				a++;
			} else {
				if (Z_TYPE_P(attr_value) != IS_LONG) {
					php_error_docref(NULL, E_WARNING, "attribute value must be integer");
					failed = 1;
					break;
				}
				vals[j] = (sphinx_int64_t)Z_LVAL_P(attr_value);
				j++;
			}
		} ZEND_HASH_FOREACH_END();

		if (failed) {
			break;
		}

		if (mva) {
			res++;
		}
		i++;
	} ZEND_HASH_FOREACH_END();

	if (!mva && i != values_num) {
		RETVAL_FALSE;
		goto cleanup;
	}

	if (!mva) {
		res = sphinx_update_attributes(c->sphinx, index, (int)attrs_num, attrs, values_num, docids, vals);
	}

	if (res < 0) {
		RETVAL_FALSE;
	} else {
		RETVAL_LONG(res);
	}

cleanup:
	efree(attrs);
	if (docids) {
		efree(docids);
	}
	if (vals) {
		efree(vals);
	}
	if (vals_mva) {
		efree(vals_mva);
	}
}
/* }}} */

/* {{{ proto array SphinxClient::buildExcerpts(array docs, string index, string words[, array opts]) */
static PHP_METHOD(SphinxClient, buildExcerpts)
{
	php_sphinx_client *c;
	zval *docs_array, *opts_array = NULL, *item;
	char *index, *words;
	const char **docs;
	sphinx_excerpt_options opts;
	size_t index_len, words_len;
	int docs_num, i = 0;
	char **result;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ass|a", &docs_array, &index, &index_len, &words, &words_len, &opts_array) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	docs_num = zend_hash_num_elements(Z_ARRVAL_P(docs_array));

	if (!docs_num) {
		php_error_docref(NULL, E_WARNING, "empty documents array passed");
		RETURN_FALSE;
	}

	docs = emalloc(sizeof(char *) * docs_num);
	ZEND_HASH_FOREACH_VAL_IND(Z_ARRVAL_P(docs_array), item) {
		if (Z_TYPE_P(item) != IS_STRING) {
			php_error_docref(NULL, E_WARNING, "non-string documents are not allowed");
			break;
		}
		docs[i] = Z_STRVAL_P(item); /* no copying here! */
		i++;
	} ZEND_HASH_FOREACH_END();

	if (i != docs_num) {
		RETVAL_FALSE;
		goto cleanup;
	}

#define OPTS_EQUAL(str, txt) str->len == sizeof(txt) && memcmp(txt, str->val, sizeof(txt)) == 0

	if (opts_array) {
		zend_string *string_key;

		/* nullify everything */
		sphinx_init_excerpt_options(&opts);
		ZEND_HASH_FOREACH_STR_KEY_VAL_IND(Z_ARRVAL_P(opts_array), string_key, item) {

			switch (Z_TYPE_P(item)) {
				case IS_STRING:
				case IS_LONG:
				case IS_TRUE:
				case IS_FALSE:
					break;
				default:
					continue; /* ignore invalid options */
			}

			if (!string_key) {
				continue; /* ignore invalid option names */
			}

			if (OPTS_EQUAL(string_key, "before_match")) {
				SEPARATE_ZVAL(item);
				convert_to_string_ex(item);
				opts.before_match = Z_STRVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "after_match")) {
				SEPARATE_ZVAL(item);
				convert_to_string_ex(item);
				opts.after_match = Z_STRVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "chunk_separator")) {
				SEPARATE_ZVAL(item);
				convert_to_string_ex(item);
				opts.chunk_separator = Z_STRVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "limit")) {
				SEPARATE_ZVAL(item);
				convert_to_long_ex(item);
				opts.limit = (int)Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "around")) {
				SEPARATE_ZVAL(item);
				convert_to_long_ex(item);
				opts.around = (int)Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "exact_phrase")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.exact_phrase = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "single_passage")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.single_passage = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "use_boundaries")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.use_boundaries = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "weight_order")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.weight_order = Z_LVAL_P(item);
#if LIBSPHINX_VERSION_ID >= 110
			} else if (OPTS_EQUAL(string_key, "query_mode")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.query_mode = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "force_all_words")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.force_all_words = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "limit_passages")) {
				SEPARATE_ZVAL(item);
				convert_to_long_ex(item);
				opts.limit_passages = (int)Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "limit_words")) {
				SEPARATE_ZVAL(item);
				convert_to_long_ex(item);
				opts.limit_words = (int)Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "start_passage_id")) {
				SEPARATE_ZVAL(item);
				convert_to_long_ex(item);
				opts.start_passage_id = (int)Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "load_files")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.load_files = Z_LVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "html_strip_mode")) {
				SEPARATE_ZVAL(item);
				convert_to_string_ex(item);
				opts.html_strip_mode = Z_STRVAL_P(item);
			} else if (OPTS_EQUAL(string_key, "allow_empty")) {
				SEPARATE_ZVAL(item);
				convert_to_boolean_ex(item);
				opts.allow_empty = Z_LVAL_P(item);
#endif
			} else {
				/* ignore invalid option names */
			}
		} ZEND_HASH_FOREACH_END();
	}

	if (opts_array) {
		result = sphinx_build_excerpts(c->sphinx, docs_num, docs, index, words, &opts);
	} else {
		result = sphinx_build_excerpts(c->sphinx, docs_num, docs, index, words, NULL);
	}

	if (!result) {
		RETVAL_FALSE;
	} else {
		array_init(return_value);
		for (i = 0; i < docs_num; i++) {
			if (result[i] && result[i][0] != '\0') {
				add_next_index_string(return_value, result[i]);
			} else {
				add_next_index_string(return_value, "");
			}
			free(result[i]);
		}
		free(result);
	}

cleanup:
	efree(docs);
}
/* }}} */

/* {{{ proto array SphinxClient::buildKeywords(string query, string index, bool hits) */
static PHP_METHOD(SphinxClient, buildKeywords)
{
	php_sphinx_client *c;
	char *query, *index;
	size_t query_len, index_len;
	zend_bool hits;
	sphinx_keyword_info *result;
	int i, num_keywords;
	zval tmp;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "ssb", &query, &query_len, &index, &index_len, &hits) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	result = sphinx_build_keywords(c->sphinx, query, index, hits, &num_keywords);
	if (!result || num_keywords <= 0) {
		RETURN_FALSE;
	}

	array_init(return_value);
	for (i = 0; i < num_keywords; i++) {
		array_init(&tmp);

		add_assoc_string(&tmp, "tokenized", result[i].tokenized);
		add_assoc_string(&tmp, "normalized", result[i].normalized);

		if (hits) {
			add_assoc_long(&tmp, "docs", result[i].num_docs);
			add_assoc_long(&tmp, "hits", result[i].num_hits);
		}

		add_next_index_zval(return_value, &tmp);

		free(result[i].tokenized);
		free(result[i].normalized);
	}
	free(result);
}
/* }}} */

/* {{{ proto void SphinxClient::resetFilters() */
static PHP_METHOD(SphinxClient, resetFilters)
{
	php_sphinx_client *c;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	sphinx_reset_filters(c->sphinx);
}
/* }}} */

/* {{{ proto void SphinxClient::resetGroupBy() */
static PHP_METHOD(SphinxClient, resetGroupBy)
{
	php_sphinx_client *c;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	sphinx_reset_groupby(c->sphinx);
}
/* }}} */

/* {{{ proto string SphinxClient::getLastWarning() */
static PHP_METHOD(SphinxClient, getLastWarning)
{
	php_sphinx_client *c;
	const char *warning;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	warning = sphinx_warning(c->sphinx);
	if (!warning || !warning[0]) {
		RETURN_EMPTY_STRING();
	}
	RETURN_STRING((char *)warning);
}
/* }}} */

/* {{{ proto string SphinxClient::getLastError() */
static PHP_METHOD(SphinxClient, getLastError)
{
	php_sphinx_client *c;
	const char *error;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	error = sphinx_error(c->sphinx);
	if (!error || !error[0]) {
		RETURN_EMPTY_STRING();
	}
	RETURN_STRING((char *)error);
}
/* }}} */

/* {{{ proto array SphinxClient::query(string query[, string index[, string comment]]) */
static PHP_METHOD(SphinxClient, query)
{
	php_sphinx_client *c;
	char *query, *index = "*", *comment = "";
	size_t query_len, index_len, comment_len;
	sphinx_result *result;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|ss", &query, &query_len, &index, &index_len, &comment, &comment_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	result = sphinx_query(c->sphinx, query, index, comment);

	if (!result) {
		RETURN_FALSE;
	}

	php_sphinx_result_to_array(c, result, return_value);
}

/* }}} */

/* {{{ proto int SphinxClient::addQuery(string query[, string index[, string comment]]) */
static PHP_METHOD(SphinxClient, addQuery)
{
	php_sphinx_client *c;
	char *query, *index = "*", *comment = "";
	size_t query_len, index_len, comment_len, res;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s|ss", &query, &query_len, &index, &index_len, &comment, &comment_len) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_add_query(c->sphinx, query, index, comment);

	if (res < 0) {
		RETURN_FALSE;
	}
	RETURN_LONG(res);
}

/* }}} */

/* {{{ proto array SphinxClient::runQueries() */
static PHP_METHOD(SphinxClient, runQueries)
{
	php_sphinx_client *c;
	sphinx_result *results;
	int i, num_results;
	zval single_result;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	results = sphinx_run_queries(c->sphinx);

	if (!results) {
		RETURN_FALSE;
	}

	num_results = sphinx_get_num_results(c->sphinx);

	array_init(return_value);
	for (i = 0; i < num_results; i++) {
		php_sphinx_result_to_array(c, &results[i], &single_result);
		add_next_index_zval(return_value, &single_result);
	}
}
/* }}} */

/* {{{ proto string SphinxClient::escapeString(string data) */
static PHP_METHOD(SphinxClient, escapeString)
{
	char *str, *new_str, *source, *target;
	size_t str_len, new_str_len, i;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "s", &str, &str_len) == FAILURE) {
		return;
	}

	if (!str_len) {
		RETURN_EMPTY_STRING();
	}

	new_str = safe_emalloc(2, str_len, 1);
	target = new_str;
	source = str;
	for (i = 0; i < str_len; i++) {
		switch (*source) {
			case '(':
			case ')':
			case '|':
			case '-':
			case '!':
			case '@':
			case '~':
			case '"':
			case '&':
			case '/':
			case '\\':
			case '^':
			case '$':
			case '=':
			case '<':
				*target++ = '\\';
				*target++ = *source;
				break;
			default:
				*target++ = *source;
				break;
		}
		source++;
	}
	*target = '\0';

	new_str_len = target - new_str;
	new_str = erealloc(new_str, new_str_len + 1);
	RETURN_STRINGL(new_str, new_str_len);
	efree(new_str);
}
/* }}} */

#if LIBSPHINX_VERSION_ID >= 99
/* {{{ proto bool SphinxClient::open() */
static PHP_METHOD(SphinxClient, open)
{
	php_sphinx_client *c;
	int res;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_open(c->sphinx);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool SphinxClient::close() */
static PHP_METHOD(SphinxClient, close)
{
	php_sphinx_client *c;
	int res;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	res = sphinx_close(c->sphinx);
	if (!res) {
		RETURN_FALSE;
	}
	RETURN_TRUE;
}
/* }}} */

/* {{{ proto array SphinxClient::status() */
static PHP_METHOD(SphinxClient, status)
{
	php_sphinx_client *c;
	char **result;
	int i, j, k, num_rows, num_cols;
	zval tmp;

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	result = sphinx_status(c->sphinx, &num_rows, &num_cols);

	if (!result || num_rows <= 0) {
		RETURN_FALSE;
	}

	k = 0;
	array_init(return_value);
	for (i = 0; i < num_rows; i++) {
		array_init(&tmp);

		for (j = 0; j < num_cols; j++, k++) {
			add_next_index_string(&tmp, result[k]);
		}
		add_next_index_zval(return_value, &tmp);
	}
	sphinx_status_destroy(result, num_rows, num_cols);
}
/* }}} */

/* {{{ proto bool SphinxClient::setOverride(string attribute, int type, array values) */
static PHP_METHOD(SphinxClient, setOverride)
{
	php_sphinx_client *c;
	zval *values, *attr_value;
	char *attribute;
	zend_long type;
	size_t attribute_len, values_num, i = 0;
	int res;
	sphinx_uint64_t *docids = NULL;
	unsigned int *vals = NULL;
	ulong id;
	zend_string *str_id;

	if (zend_parse_parameters(ZEND_NUM_ARGS(), "sla", &attribute, &attribute_len, &type, &values) == FAILURE) {
		return;
	}

	c = sphinx_client(getThis());
	SPHINX_INITIALIZED(c)

	if (type != SPH_ATTR_INTEGER && type != SPH_ATTR_TIMESTAMP
		&& type != SPH_ATTR_BOOL && type != SPH_ATTR_FLOAT) {
		php_error_docref(NULL, E_WARNING, "type must be scalar");
		RETURN_FALSE;
	}

	values_num = zend_hash_num_elements(Z_ARRVAL_P(values));
	if (!values_num) {
		php_error_docref(NULL, E_WARNING, "empty values array passed");
		RETURN_FALSE;
	}

	docids = emalloc(sizeof(sphinx_uint64_t) * values_num);
	vals = safe_emalloc(values_num, sizeof(unsigned int), 0);
	ZEND_HASH_FOREACH_KEY_VAL_IND(Z_ARRVAL_P(values), id, str_id, attr_value) {
		double float_id = 0;
		unsigned char id_type;

		if (Z_TYPE_P(attr_value) != IS_LONG) {
			php_error_docref(NULL, E_WARNING, "attribute value must be integer");
			break;
		}

		if (!str_id) {
			/* ok */
			id_type = IS_LONG;
		} else {
			id_type = is_numeric_string(str_id->val, str_id->len, (long *)&id, &float_id, 0);
			if (id_type == IS_LONG || id_type == IS_DOUBLE) {
				/* ok */
			} else {
				php_error_docref(NULL, E_WARNING, "document ID must be numeric");
				break;
			}
		}
		vals[i] = (sphinx_uint64_t)Z_LVAL_P(attr_value);

		if (id_type == IS_LONG) {
			docids[i] = (sphinx_uint64_t)id;
		} else { /* IS_FLOAT */
			docids[i] = (sphinx_uint64_t)float_id;
		}
		i++;
	} ZEND_HASH_FOREACH_END();

	if (i != values_num) {
		RETVAL_FALSE;
		goto cleanup;
	}

	res = sphinx_add_override(c->sphinx, attribute, docids, values_num, vals);
	if (!res) {
		RETVAL_FALSE;
	} else {
		RETVAL_TRUE;
	}

cleanup:
	if (docids) {
		efree(docids);
	}
	if (vals) {
		efree(vals);
	}
}
/* }}} */
#endif

/* {{{ proto int SphinxClient::__sleep() */
static PHP_METHOD(SphinxClient, __sleep)
{
	php_error_docref(NULL, E_RECOVERABLE_ERROR, "SphinxClient instance cannot be (un)serialized");
}
/* }}} */

/* {{{ proto int SphinxClient::__wakeup() */
static PHP_METHOD(SphinxClient, __wakeup)
{
	php_error_docref(NULL, E_RECOVERABLE_ERROR, "SphinxClient instance cannot be (un)serialized");
}
/* }}} */

/* {{{ arginfo */
ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setserver, 0, 0, 2)
	ZEND_ARG_INFO(0, server)
	ZEND_ARG_INFO(0, port)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setlimits, 0, 0, 2)
	ZEND_ARG_INFO(0, offset)
	ZEND_ARG_INFO(0, limit)
	ZEND_ARG_INFO(0, max_matches)
	ZEND_ARG_INFO(0, cutoff)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setmatchmode, 0, 0, 1)
	ZEND_ARG_INFO(0, mode)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setindexweights, 0, 0, 1)
	ZEND_ARG_INFO(0, weights)
ZEND_END_ARG_INFO()

#if LIBSPHINX_VERSION_ID >= 99
ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setselect, 0, 0, 1)
	ZEND_ARG_INFO(0, clause)
ZEND_END_ARG_INFO()
#endif

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setidrange, 0, 0, 2)
	ZEND_ARG_INFO(0, min)
	ZEND_ARG_INFO(0, max)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setfilter, 0, 0, 2)
	ZEND_ARG_INFO(0, attribute)
	ZEND_ARG_INFO(0, values)
	ZEND_ARG_INFO(0, exclude)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setfilterstring, 0, 0, 2)
	ZEND_ARG_INFO(0, attribute)
	ZEND_ARG_INFO(0, value)
	ZEND_ARG_INFO(0, exclude)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setfilterrange, 0, 0, 3)
	ZEND_ARG_INFO(0, attribute)
	ZEND_ARG_INFO(0, min)
	ZEND_ARG_INFO(0, max)
	ZEND_ARG_INFO(0, exclude)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setgeoanchor, 0, 0, 4)
	ZEND_ARG_INFO(0, attrlat)
	ZEND_ARG_INFO(0, attrlong)
	ZEND_ARG_INFO(0, latitude)
	ZEND_ARG_INFO(0, longitude)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setgroupby, 0, 0, 2)
	ZEND_ARG_INFO(0, attribute)
	ZEND_ARG_INFO(0, func)
	ZEND_ARG_INFO(0, groupsort)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setgroupdistinct, 0, 0, 1)
	ZEND_ARG_INFO(0, attribute)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setretries, 0, 0, 1)
	ZEND_ARG_INFO(0, count)
	ZEND_ARG_INFO(0, delay)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setmaxquerytime, 0, 0, 1)
	ZEND_ARG_INFO(0, qtime)
ZEND_END_ARG_INFO()

#if LIBSPHINX_VERSION_ID >= 99
ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setoverride, 0, 0, 3)
	ZEND_ARG_INFO(0, attribute)
	ZEND_ARG_INFO(0, type)
	ZEND_ARG_INFO(0, values)
ZEND_END_ARG_INFO()
#endif

#if HAVE_3ARG_SPHINX_SET_RANKING_MODE
ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setrankingmode, 0, 0, 1)
	ZEND_ARG_INFO(0, ranker)
	ZEND_ARG_INFO(0, rank_expression)
ZEND_END_ARG_INFO()
#else
ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setrankingmode, 0, 0, 1)
	ZEND_ARG_INFO(0, ranker)
ZEND_END_ARG_INFO()
#endif

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setsortmode, 0, 0, 1)
	ZEND_ARG_INFO(0, mode)
	ZEND_ARG_INFO(0, sortby)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setconnecttimeout, 0, 0, 1)
	ZEND_ARG_INFO(0, timeout)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_setarrayresult, 0, 0, 1)
	ZEND_ARG_INFO(0, array_result)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_updateattributes, 0, 0, 3)
	ZEND_ARG_INFO(0, index)
	ZEND_ARG_INFO(0, attributes)
	ZEND_ARG_INFO(0, values)
	ZEND_ARG_INFO(0, mva)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_buildexcerpts, 0, 0, 3)
	ZEND_ARG_INFO(0, docs)
	ZEND_ARG_INFO(0, index)
	ZEND_ARG_INFO(0, words)
	ZEND_ARG_INFO(0, opts)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_buildkeywords, 0, 0, 3)
	ZEND_ARG_INFO(0, query)
	ZEND_ARG_INFO(0, index)
	ZEND_ARG_INFO(0, hits)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_query, 0, 0, 1)
	ZEND_ARG_INFO(0, query)
	ZEND_ARG_INFO(0, index)
	ZEND_ARG_INFO(0, comment)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO(arginfo_sphinxclient__param_void, 0)
ZEND_END_ARG_INFO()

ZEND_BEGIN_ARG_INFO_EX(arginfo_sphinxclient_escapestring, 0, 0, 1)
	ZEND_ARG_INFO(0, data)
ZEND_END_ARG_INFO()
/* }}} */

static zend_function_entry sphinx_client_methods[] = { /* {{{ */
	PHP_ME(SphinxClient, __construct, 			arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, addQuery, 				arginfo_sphinxclient_query, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, buildExcerpts, 		arginfo_sphinxclient_buildexcerpts, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, buildKeywords, 		arginfo_sphinxclient_buildkeywords, ZEND_ACC_PUBLIC)
#if LIBSPHINX_VERSION_ID >= 99
	PHP_ME(SphinxClient, close, 				arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, getLastError, 			arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, getLastWarning, 		arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, escapeString, 			arginfo_sphinxclient_escapestring, ZEND_ACC_PUBLIC)
#if LIBSPHINX_VERSION_ID >= 99
	PHP_ME(SphinxClient, open, 					arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, query, 				arginfo_sphinxclient_query, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, resetFilters, 			arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, resetGroupBy, 			arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, runQueries, 			arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setArrayResult, 		arginfo_sphinxclient_setarrayresult, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setConnectTimeout, 	arginfo_sphinxclient_setconnecttimeout, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setFieldWeights, 		arginfo_sphinxclient_setindexweights, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setFilter, 			arginfo_sphinxclient_setfilter, ZEND_ACC_PUBLIC)
#ifdef HAVE_SPHINX_ADD_FILTER_STRING
	PHP_ME(SphinxClient, setFilterString, 		arginfo_sphinxclient_setfilterstring, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, setFilterFloatRange, 	arginfo_sphinxclient_setfilterrange, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setFilterRange, 		arginfo_sphinxclient_setfilterrange, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setGeoAnchor, 			arginfo_sphinxclient_setgeoanchor, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setGroupBy, 			arginfo_sphinxclient_setgroupby, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setGroupDistinct, 		arginfo_sphinxclient_setgroupdistinct, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setIndexWeights, 		arginfo_sphinxclient_setindexweights, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setIDRange, 			arginfo_sphinxclient_setidrange, ZEND_ACC_PUBLIC)
#if LIBSPHINX_VERSION_ID >= 99
	PHP_ME(SphinxClient, setSelect, 			arginfo_sphinxclient_setselect, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, setLimits, 			arginfo_sphinxclient_setlimits, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setMatchMode, 			arginfo_sphinxclient_setmatchmode, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setMaxQueryTime, 		arginfo_sphinxclient_setmaxquerytime, ZEND_ACC_PUBLIC)
#if LIBSPHINX_VERSION_ID >= 99
	PHP_ME(SphinxClient, setOverride, 			arginfo_sphinxclient_setoverride, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, setRankingMode, 		arginfo_sphinxclient_setrankingmode, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setRetries, 			arginfo_sphinxclient_setretries, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setServer, 			arginfo_sphinxclient_setserver, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, setSortMode, 			arginfo_sphinxclient_setsortmode, ZEND_ACC_PUBLIC)
#if LIBSPHINX_VERSION_ID >= 99
	PHP_ME(SphinxClient, status, 				arginfo_sphinxclient__param_void, ZEND_ACC_PUBLIC)
#endif
	PHP_ME(SphinxClient, updateAttributes, 		arginfo_sphinxclient_updateattributes, ZEND_ACC_PUBLIC)
	PHP_ME(SphinxClient, __sleep,				NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
	PHP_ME(SphinxClient, __wakeup,				NULL, ZEND_ACC_PUBLIC|ZEND_ACC_FINAL)
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ PHP_MINIT_FUNCTION
 */
PHP_MINIT_FUNCTION(sphinx)
{
	zend_class_entry ce;

	memcpy(&cannot_be_cloned, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	cannot_be_cloned.clone_obj = NULL;

	memcpy(&php_sphinx_client_handlers, zend_get_std_object_handlers(), sizeof(zend_object_handlers));
	php_sphinx_client_handlers.clone_obj = NULL;
	php_sphinx_client_handlers.read_property = php_sphinx_client_read_property;
	php_sphinx_client_handlers.get_properties = php_sphinx_client_get_properties;
	php_sphinx_client_handlers.free_obj = php_sphinx_client_obj_dtor;
	php_sphinx_client_handlers.offset = XtOffsetOf(php_sphinx_client, std);

	INIT_CLASS_ENTRY(ce, "SphinxClient", sphinx_client_methods);
	ce_sphinx_client = zend_register_internal_class(&ce);
	ce_sphinx_client->create_object = php_sphinx_client_new;

	SPHINX_CONST(SEARCHD_OK);
	SPHINX_CONST(SEARCHD_ERROR);
	SPHINX_CONST(SEARCHD_RETRY);
	SPHINX_CONST(SEARCHD_WARNING);

	SPHINX_CONST(SPH_MATCH_ALL);
	SPHINX_CONST(SPH_MATCH_ANY);
	SPHINX_CONST(SPH_MATCH_PHRASE);
	SPHINX_CONST(SPH_MATCH_BOOLEAN);
	SPHINX_CONST(SPH_MATCH_EXTENDED);
	SPHINX_CONST(SPH_MATCH_FULLSCAN);
	SPHINX_CONST(SPH_MATCH_EXTENDED2);

	SPHINX_CONST(SPH_RANK_PROXIMITY_BM25);
	SPHINX_CONST(SPH_RANK_BM25);
	SPHINX_CONST(SPH_RANK_NONE);
	SPHINX_CONST(SPH_RANK_WORDCOUNT);
#ifdef HAVE_SPH_RANK_PROXIMITY
	SPHINX_CONST(SPH_RANK_PROXIMITY);
#endif
#ifdef HAVE_SPH_RANK_MATCHANY
	SPHINX_CONST(SPH_RANK_MATCHANY);
#endif
#ifdef HAVE_SPH_RANK_FIELDMASK
	SPHINX_CONST(SPH_RANK_FIELDMASK);
#endif
#ifdef HAVE_SPH_RANK_SPH04
	SPHINX_CONST(SPH_RANK_SPH04);
#endif
#ifdef HAVE_SPH_RANK_EXPR
	SPHINX_CONST(SPH_RANK_EXPR);
#endif
#ifdef HAVE_SPH_RANK_TOTAL
	SPHINX_CONST(SPH_RANK_TOTAL);
#endif

	SPHINX_CONST(SPH_SORT_RELEVANCE);
	SPHINX_CONST(SPH_SORT_ATTR_DESC);
	SPHINX_CONST(SPH_SORT_ATTR_ASC);
	SPHINX_CONST(SPH_SORT_TIME_SEGMENTS);
	SPHINX_CONST(SPH_SORT_EXTENDED);
	SPHINX_CONST(SPH_SORT_EXPR);

	SPHINX_CONST(SPH_FILTER_VALUES);
	SPHINX_CONST(SPH_FILTER_RANGE);
	SPHINX_CONST(SPH_FILTER_FLOATRANGE);

	SPHINX_CONST(SPH_ATTR_INTEGER);
	SPHINX_CONST(SPH_ATTR_TIMESTAMP);
	SPHINX_CONST(SPH_ATTR_ORDINAL);
	SPHINX_CONST(SPH_ATTR_BOOL);
	SPHINX_CONST(SPH_ATTR_FLOAT);
	SPHINX_CONST(SPH_ATTR_MULTI);

	SPHINX_CONST(SPH_GROUPBY_DAY);
	SPHINX_CONST(SPH_GROUPBY_WEEK);
	SPHINX_CONST(SPH_GROUPBY_MONTH);
	SPHINX_CONST(SPH_GROUPBY_YEAR);
	SPHINX_CONST(SPH_GROUPBY_ATTR);
	SPHINX_CONST(SPH_GROUPBY_ATTRPAIR);

	return SUCCESS;
}
/* }}} */

/* {{{ PHP_MINFO_FUNCTION
 */
PHP_MINFO_FUNCTION(sphinx)
{
	php_info_print_table_start();
	php_info_print_table_header(2, "sphinx support", "enabled");
	php_info_print_table_header(2, "Version", PHP_SPHINX_VERSION);
	php_info_print_table_header(2, "Revision", "$Revision$");
	php_info_print_table_end();
}
/* }}} */

static zend_function_entry sphinx_functions[] = { /* {{{ */
	{NULL, NULL, NULL}
};
/* }}} */

/* {{{ sphinx_module_entry
 */
zend_module_entry sphinx_module_entry = {
	STANDARD_MODULE_HEADER,
	"sphinx",
	sphinx_functions,
	PHP_MINIT(sphinx),
	NULL,
	NULL,
	NULL,
	PHP_MINFO(sphinx),
	PHP_SPHINX_VERSION,
	STANDARD_MODULE_PROPERTIES
};
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */
