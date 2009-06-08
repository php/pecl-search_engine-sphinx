dnl $Id$

PHP_ARG_WITH(sphinx, for sphinx support,
[  --with-sphinx             Include sphinx support])

if test "$PHP_SPHINX" != "no"; then

  SEARCH_PATH="/usr/local /usr /local /opt"
  SEARCH_FOR="/include/sphinxclient.h"

  if test "$PHP_SPHINX" = "yes"; then
    AC_MSG_CHECKING([for libsphinxclient headers in default path])
    for i in $SEARCH_PATH ; do
      if test -r $i/$SEARCH_FOR; then
        SPHINX_DIR=$i
        AC_MSG_RESULT(found in $i)
      fi
    done
  else 
    AC_MSG_CHECKING([for libsphinxclient headers in $PHP_SPHINX])
	if test -r $PHP_SPHINX/$SEARCH_FOR; then
	  SPHINX_DIR=$PHP_SPHINX
      AC_MSG_RESULT([found])
	fi
  fi

  if test -z "$SPHINX_DIR"; then
    AC_MSG_RESULT([not found])
    AC_MSG_ERROR([Cannot find libsphinxclient headers])
  fi

  PHP_ADD_INCLUDE($SPHINX_DIR/include)

  LIBNAME=sphinxclient
  LIBSYMBOL=sphinx_create

  PHP_CHECK_LIBRARY($LIBNAME,$LIBSYMBOL,
  [
    PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $SPHINX_DIR/$PHP_LIBDIR, SPHINX_SHARED_LIBADD)
    AC_DEFINE(HAVE_SPHINXLIB,1,[ ])
  ],[
    AC_MSG_ERROR([wrong libsphinxclient version or lib not found])
  ],[
    -L$SPHINX_DIR/$PHP_LIBDIR -lm
  ])
  
  PHP_CHECK_LIBRARY($LIBNAME,sphinx_set_select,
  [
    PHP_ADD_LIBRARY_WITH_PATH($LIBNAME, $SPHINX_DIR/$PHP_LIBDIR, SPHINX_SHARED_LIBADD)
    AC_DEFINE(HAVE_SPHINX_SET_SELECT,1,[ ])
  ],[],[
    -L$SPHINX_DIR/$PHP_LIBDIR -lm
  ])
  
  PHP_SUBST(SPHINX_SHARED_LIBADD)

  PHP_NEW_EXTENSION(sphinx, sphinx.c, $ext_shared)
fi
