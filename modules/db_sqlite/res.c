/**
 *
 * Copyright (C) 2015 OpenSIPS Foundation
 *
 * This file is part of opensips, a free SIP server.
 *
 * opensips is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version
 *
 * opensips is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * History
 * -------
 *  2015-03-03  initial version (Ionut Ionita)
*/


#define DWORD(p) (*(p+0) + (*(p+1) << 8) + (*(p+2) << 16) + (*(p+3) << 24))
#define DWORD_LEN	4
#include <string.h>
#include <sqlite3.h>
#include "../../db/db_query.h"
#include "../../db/db_async.h"
#include "../../db/db_ut.h"
#include "../../db/db_insertq.h"
#include "../../db/db_res.h"
#include "../../ut.h"
#include "my_con.h"
#include "row.h"


extern int db_sqlite_alloc_limit;
static db_type_t get_type_from_decltype(const char *decltype)
{
	/* DB_INT datatypes*/
	#define INT		0x00746e69				/* INT */
	#define INTE	0x65746e69				/* INTEGER */
	#define TINY	0x796e6974				/* TINYINT */
	#define SMAL	0x6c616d73				/* SMALLINT */
	#define MEDI	0x6964656d				/* MEDIUMINT */
	#define BIGI	0x69676962				/* BIGINT */
	#define UNSI	0x69736e75				/* UNSIGNED BIG INT */
	#define INT2	0x32746e69				/* INT2 */
	#define INT8	0x38746e69				/* INT8 */
	#define NUME	0x656d756e				/* NUMERIC */
	#define BOOL	0x6c6f6f62				/* BOOLEAN */
	#define DECI	0x69636564				/* DECIMAL */
	/*******************/

	/* DB_STRING datatypes */
	#define CHAR	0x72616863				/* CHARACTER*/
	#define VARC	0x63726176				/* VARCHAR */
	#define VARY	0x79726176				/* VARYING CHARACTER */
	#define NCHA	0x6168636e				/* NCHAR */
	#define NATI	0x6974616e				/* NATIVE CHARACTER */
	#define NVAR	0x7261766e				/* NVARCHAR */
	#define TEXT	0x74786574				/* TEXT */
	#define CLOB	0x626f6c63				/* CLOB */
	/***********************/

	/* DB_BLOB datatypes */
	#define BLOB	0x626f6c62				/* BLOB */
	/*********************/

	/* DB_DOUBLE datatypes */
	#define REAL	0x6c616572				/* REAL */
	#define DOUB	0x62756f64				/* DUBLE or DOULBE PRECISION */
	#define FLOA	0x616f6c66				/* FLOAT */
	/***********************/

	/* DB_DATETIME datatypes */
	#define DATE	0x65746164				/* DATE or DATETIME */
	/*************************/

	str s;
	char dword[DWORD_LEN];

	s.s = dword;
	s.len = DWORD_LEN;

	/* avoid memory corruption if decltype size is smaller than DWORD_LEN */
	memset(s.s, 0, DWORD_LEN);
	memcpy(s.s, decltype, s.len);

	strlower(&s);

	switch (DWORD(s.s)) {
		case INT:
		case INTE:
		case TINY:
		case SMAL:
		case MEDI:
		case BIGI:
		case UNSI:
		case INT2:
		case INT8:
		case NUME:
		case BOOL:
		case DECI:
			return DB_INT;
		case CHAR:
		case VARC:
		case VARY:
		case NCHA:
		case NATI:
		case NVAR:
		case TEXT:
		case CLOB:
			return DB_STRING;
		case BLOB:
			return DB_BLOB;
		case REAL:
		case DOUB:
		case FLOA:
			return DB_DOUBLE;
		case DATE:
			return DB_DATETIME;
		default:
			/* check again for INT */
			if ((DWORD(s.s) & 0x00FFFFFF) == INT)
				return DB_INT;
			LM_BUG("invalid datatype! this should not happen "
					"since all sqlite datatypes are defined here!\n");
			return -1;
	}

	return 0;
	/* undefine all datatypes; let someone else use them if needed */
	/* DB_INT datatypes*/
	#undef INT
	#undef INTE
	#undef TINY
	#undef SMAL
	#undef MEDI
	#undef BIGI
	#undef UNSI
	#undef INT2
	#undef INT8
	#undef NUME
	#undef BOOL
	#undef DECI
	/*******************/

	/* DB_STRING datatypes */
	#undef CHAR
	#undef VARC
	#undef VARY
	#undef NCHA
	#undef NATI
	#undef NVAR
	#undef TEXT
	#undef CLOB
	/***********************/

	/* DB_BLOB datatypes */
	#undef BLOB
	/*********************/

	/* DB_DOUBLE datatypes */
	#undef REAL
	#undef DOUB
	#undef FLOA
	/***********************/

	/* DB_DATETIME datatypes */
	#undef DATE
	/*************************/

}

/**
 * Get and convert columns from a result
 */
int db_sqlite_get_columns(const db_con_t* _h, db_res_t* _r)
{
	int col;
	const char *decltype;
	const char* name;

	if ((!_h) || (!_r)) {
		LM_ERR("invalid parameter\n");
		return -1;
	}

	if (CON_HAS_PS(_h))
		RES_COL_N(_r) = sqlite3_column_count(CON_PS_STMT(_h));
	else
		goto ps_err;

	if (!RES_COL_N(_r)) {
		LM_ERR("no columns returned from the query\n");
		return -2;
	} else {
		LM_DBG("%d columns returned from the query\n", RES_COL_N(_r));
	}

	if (db_allocate_columns(_r, RES_COL_N(_r)) != 0) {
		LM_ERR("could not allocate columns\n");
		return -3;
	}

	for(col = 0; col < RES_COL_N(_r); col++) {
		/* The pointer that is here returned is part of the result structure */

		name = sqlite3_column_name(CON_PS_STMT(_h), col);
		RES_NAMES(_r)[col]->s = *((char**)&name);
		RES_NAMES(_r)[col]->len = strlen(RES_NAMES(_r)[col]->s);

		decltype = sqlite3_column_decltype(CON_PS_STMT(_h), col);
		RES_TYPES(_r)[col]	= get_type_from_decltype(decltype);

		LM_DBG("RES_NAMES(%p)[%d]=[%.*s]\n", RES_NAMES(_r)[col], col,
				RES_NAMES(_r)[col]->len, RES_NAMES(_r)[col]->s);

		/* types will be determined at runtime */
	}
	return 0;
ps_err:
	LM_ERR("conn has no query! invalid usage\n");
	return -1;
}


/*
 * specific alloc type for this module
 * it's easier to realloc
 */
int db_sqlite_allocate_rows(db_res_t* res, const unsigned int rows)
{
	unsigned int i;

	/* first allocate the rows */
	RES_ROWS(res) = (struct db_row*)pkg_malloc(rows * (sizeof(db_row_t)));
	memset( RES_ROWS(res), 0 ,
		rows * (sizeof(db_row_t) + sizeof(db_val_t) * RES_COL_N(res)));

	if (!RES_ROWS(res))
		goto out;


	/* and then the values */
	ROW_VALUES( &(RES_ROWS(res)[0]) ) =
		pkg_malloc(rows * sizeof(db_val_t) * RES_COL_N(res));
//	memset( ROW_VALUES( &(RES_ROWS(res)[0]) ), 0,
//				sizeof(db_val_t) * RES_COL_N(res));

	if (! ROW_VALUES( &(RES_ROWS(res)[0]) ))
		goto out;

	for( i=1 ; i<rows ; i++ )
		/* the values of the row i */
		ROW_VALUES( &(RES_ROWS(res)[i]) ) =
				ROW_VALUES( &(RES_ROWS(res)[0]) )+RES_COL_N(res)*i;

	return 0;
out:
	LM_ERR("no memory left\n");
	return -1;
}

/*
 * realloc function
 * used when entries added to the db since count(*) query
 */
int db_sqlite_realloc_rows(db_res_t* res, const unsigned int rows)
{
	unsigned int i;

	RES_ROWS(res) = pkg_realloc(RES_ROWS(res),rows * (sizeof(db_row_t)));
	memset( RES_ROWS(res) + RES_ROW_N(res), 0 ,
			(rows - RES_ROW_N(res)) * (sizeof(db_row_t)));

	if (!RES_ROWS(res)) {
		LM_ERR("no memory left\n");
		return -1;
	}

	ROW_VALUES( &(RES_ROWS(res)[0]) ) =
		pkg_realloc(ROW_VALUES( &(RES_ROWS(res)[0]) ),
				rows * sizeof(db_val_t) * RES_COL_N(res));
//	memset( ROW_VALUES( &(RES_ROWS(res)[RES_ROW_N(res)]) ), 0,
//		(rows - RES_ROW_N(res)) * sizeof(db_val_t) * RES_COL_N(res));

	if (! ROW_VALUES( &(RES_ROWS(res)[0]) )) {
		LM_ERR("no memory left\n");
		return -1;
	}

	for( i=RES_ROW_N(res) ; i<rows ; i++ )
		/* the values of the row i */
		ROW_VALUES( &(RES_ROWS(res)[i]) ) =
				ROW_VALUES( &(RES_ROWS(res)[0]) )+RES_COL_N(res)*i;

	return 0;

}

/**
 * Convert rows from sqlite to db API representation
 *
 */
static inline int db_sqlite_convert_rows(const db_con_t* _h, db_res_t* _r)
{
	int row;
	int ret;

	if ((!_h) || (!_r)) {
		LM_ERR("invalid parameter\n");
		return -1;
	}

	if (!CON_HAS_PS(_h)) {
		LM_ERR(" all sqlite queries should have a ps!\n");
		return -1;
	}

	if (!RES_ROW_N(_r)) {
		LM_DBG("no rows returned from the query\n");
		RES_ROWS(_r) = 0;
		return 0;
	}

	if (db_sqlite_allocate_rows( _r, RES_ROW_N(_r))!=0) {
		LM_ERR("no private memory left\n");
		return -2;
	}

	row=0;
	ret=-1;


	while (ret != SQLITE_DONE) {
		ret = sqlite3_step(CON_PS_STMT(_h));
		if (row == 0 && ret == SQLITE_BUSY)
			continue;

		if (ret == SQLITE_DONE) {
			RES_ROW_N(_r) = RES_LAST_ROW(_r) = RES_NUM_ROWS(_r) = row;
			sqlite3_reset(CON_PS_STMT(_h));
			break;
		}

		if (row == RES_ROW_N(_r)) {
			db_sqlite_realloc_rows(_r, RES_ROW_N(_r) + db_sqlite_alloc_limit);
			RES_ROW_N(_r) += db_sqlite_alloc_limit;
		}

		if ((ret=db_sqlite_convert_row(_h, _r, &(RES_ROWS(_r)[row]))) < 0) {
			LM_ERR("error while converting row #%d\n", row);
			RES_ROW_N(_r) = row;
			db_free_rows(_r);
			return -4;
		}

		row++;
	}

	return ret;
}


/**
 * Fill the structure with data from database
 */
int db_sqlite_convert_result(const db_con_t* _h, db_res_t* _r)
{
	if ((!_h) || (!_r)) {
		LM_ERR("invalid parameter\n");
		return -1;
	}

	if (db_sqlite_get_columns(_h, _r) < 0) {
		LM_ERR("error while getting column names\n");
		return -2;
	}

	if (db_sqlite_convert_rows(_h, _r) < 0) {
		LM_ERR("error while converting rows\n");
		db_free_columns(_r);
		return -3;
	}

	LM_INFO("fetching columns\n");
	return 0;
}

#undef DWORD