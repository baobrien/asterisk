/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * Utility functions
 *
 * Copyright (C) 2004 - 2005, Digium, Inc.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 */

#ifndef _ASTERISK_UTILS_H
#define _ASTERISK_UTILS_H

#ifdef SOLARIS
#include <solaris-compat/compat.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>	/* we want to override inet_ntoa */
#include <netdb.h>
#include <limits.h>

#include "asterisk/lock.h"

/* Note:
   It is very important to use only unsigned variables to hold
   bit flags, as otherwise you can fall prey to the compiler's
   sign-extension antics if you try to use the top two bits in
   your variable.

   The flag macros below use a set of compiler tricks to verify
   that the caller is using an "unsigned int" variable to hold
   the flags, and nothing else. If the caller uses any other
   type of variable, a warning message similar to this:

   warning: comparison of distinct pointer types lacks cast

   will be generated.

   The "dummy" variable below is used to make these comparisons.

   Also note that at -O2 or above, this type-safety checking
   does _not_ produce any additional object code at all.
*/

extern unsigned int __unsigned_int_flags_dummy;

#define ast_test_flag(p,flag) 		({ \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags & (flag)); \
					})

#define ast_set_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags |= (flag)); \
					} while(0)

#define ast_clear_flag(p,flag) 		do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					((p)->flags &= ~(flag)); \
					} while(0)

#define ast_copy_flags(dest,src,flagz)	do { \
					typeof ((dest)->flags) __d = (dest)->flags; \
					typeof ((src)->flags) __s = (src)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__d == &__x); \
					(void) (&__s == &__x); \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define ast_set2_flag(p,value,flag)	do { \
					typeof ((p)->flags) __p = (p)->flags; \
					typeof (__unsigned_int_flags_dummy) __x = 0; \
					(void) (&__p == &__x); \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

/* Non-type checking variations for non-unsigned int flags.  You
   should only use non-unsigned int flags where required by 
   protocol etc and if you know what you're doing :)  */
#define ast_test_flag_nonstd(p,flag) 		({ \
					((p)->flags & (flag)); \
					})

#define ast_set_flag_nonstd(p,flag) 		do { \
					((p)->flags |= (flag)); \
					} while(0)

#define ast_clear_flag_nonstd(p,flag) 		do { \
					((p)->flags &= ~(flag)); \
					} while(0)

#define ast_copy_flags_nonstd(dest,src,flagz)	do { \
					(dest)->flags &= ~(flagz); \
					(dest)->flags |= ((src)->flags & (flagz)); \
					} while (0)

#define ast_set2_flag_nonstd(p,value,flag)	do { \
					if (value) \
						(p)->flags |= (flag); \
					else \
						(p)->flags &= ~(flag); \
					} while (0)

#define AST_FLAGS_ALL UINT_MAX

struct ast_flags {
	unsigned int flags;
};

static inline int ast_strlen_zero(const char *s)
{
	return (*s == '\0');
}

struct ast_hostent {
	struct hostent hp;
	char buf[1024];
};

/*!
  \brief Gets a pointer to the first non-whitespace character in a string.
  \param str the input string
  \return a pointer to the first non-whitespace character
 */
char *ast_skip_blanks(char *str);

/*!
  \brief Trims trailing whitespace characters from a string.
  \param str the input string
  \return a pointer to the NULL following the string
 */
char *ast_trim_blanks(char *str);

/*!
  \brief Gets a pointer to first whitespace character in a string.
  \param str the input string
  \return a pointer to the first whitespace character
 */
char *ast_skip_nonblanks(char *str);
  
/*!
  \brief Strip leading/trailing whitespace from a string.
  \param s The string to be stripped (will be modified).
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.
*/
char *ast_strip(char *s);

/*!
  \brief Strip leading/trailing whitespace and quotes from a string.
  \param s The string to be stripped (will be modified).
  \param beg_quotes The list of possible beginning quote characters.
  \param end_quotes The list of matching ending quote characters.
  \return The stripped string.

  This functions strips all leading and trailing whitespace
  characters from the input string, and returns a pointer to
  the resulting string. The string is modified in place.

  It can also remove beginning and ending quote (or quote-like)
  characters, in matching pairs. If the first character of the
  string matches any character in beg_quotes, and the last
  character of the string is the matching character in
  end_quotes, then they are removed from the string.

  Examples:
  \code
  ast_strip_quoted(buf, "\"", "\"");
  ast_strip_quoted(buf, "'", "'");
  ast_strip_quoted(buf, "[{(", "]})");
  \endcode
 */
char *ast_strip_quoted(char *s, const char *beg_quotes, const char *end_quotes);

extern struct hostent *ast_gethostbyname(const char *host, struct ast_hostent *hp);
/* ast_md5_hash: Produces MD5 hash based on input string */
extern void ast_md5_hash(char *output, char *input);
extern int ast_base64encode(char *dst, unsigned char *src, int srclen, int max);
extern int ast_base64decode(unsigned char *dst, char *src, int max);

extern int test_for_thread_safety(void);

extern const char *ast_inet_ntoa(char *buf, int bufsiz, struct in_addr ia);
extern int ast_utils_init(void);
extern int ast_wait_for_input(int fd, int ms);

/* The realloca lets us ast_restrdupa(), but you can't mix any other ast_strdup calls! */

struct ast_realloca {
	char *ptr;
	int alloclen;
};

#define ast_restrdupa(ra, s) \
	({ \
		if ((ra)->ptr && strlen(s) + 1 < (ra)->alloclen) { \
			strcpy((ra)->ptr, s); \
		} else { \
			(ra)->ptr = alloca(strlen(s) + 1 - (ra)->alloclen); \
			if ((ra)->ptr) (ra)->alloclen = strlen(s) + 1; \
		} \
		(ra)->ptr; \
	})

#ifdef inet_ntoa
#undef inet_ntoa
#endif
#define inet_ntoa __dont__use__inet_ntoa__use__ast_inet_ntoa__instead__

#define AST_STACKSIZE 256 * 1024
#define ast_pthread_create(a,b,c,d) ast_pthread_create_stack(a,b,c,d,0)
extern int ast_pthread_create_stack(pthread_t *thread, pthread_attr_t *attr, void *(*start_routine)(void *), void *data, size_t stacksize);

#ifdef __linux__
#define ast_strcasestr strcasestr
#else
extern char *ast_strcasestr(const char *, const char *);
#endif /* __linux__ */

#if __GNUC__ < 2 || (__GNUC__ == 2 && __GNUC_MINOR__ < 96)
#define __builtin_expect(exp, c) (exp)
#endif

/*!
  \brief Size-limited null-terminating string copy.
  \param dst The destination buffer.
  \param src The source string
  \param size The size of the destination buffer
  \return Nothing.

  This is similar to \a strncpy, with two important differences:
    - the destination buffer will \b always be null-terminated
    - the destination buffer is not filled with zeros past the copied string length
  These differences make it slightly more efficient, and safer to use since it will
  not leave the destination buffer unterminated. There is no need to pass an artificially
  reduced buffer size to this function (unlike \a strncpy), and the buffer does not need
  to be initialized to zeroes prior to calling this function.
*/
void ast_copy_string(char *dst, const char *src, size_t size);

/*!
  \brief Build a string in a buffer, designed to be called repeatedly
  
  This is a wrapper for snprintf, that properly handles the buffer pointer
  and buffer space available.

  \return 0 on success, non-zero on failure.
  \param buffer current position in buffer to place string into (will be updated on return)
  \param space remaining space in buffer (will be updated on return)
  \param fmt printf-style format string
*/
int ast_build_string(char **buffer, size_t *space, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)));

/* functions for working with 'struct timeval' instances */

/*!
 * \brief Computes the difference (in milliseconds) between two \c struct \c timeval instances.
 * \param start the beginning of the time period
 * \param end the end of the time period
 * \return the difference in milliseconds
 */
static inline int ast_tvdiff_ms(struct timeval *start, struct timeval *end)
{
	return ((end->tv_sec - start->tv_sec) * 1000) + ((end->tv_usec - start->tv_usec) / 1000);
}

#endif /* _ASTERISK_UTILS_H */
