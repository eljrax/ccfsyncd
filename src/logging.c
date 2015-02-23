#include "ccfsync.h"
#include <syslog.h>

void
init_logging ()
{

  if (cfg->syslog) {
    openlog (NULL, LOG_CONS | LOG_PID, LOG_USER);
  }

  if (cfg->log_file != NULL) {
    log_fp = fopen (cfg->log_file, "a+");
    if (log_fp == NULL) {
      fprintf (stderr, "Failed to open logfile %s: (%s) Disabling file logging....\n", cfg->log_file, strerror (errno));
      cfg->log_file = NULL;
    }
  }

}

/* Returns current date and time in locale, strips newline */
char *
get_current_time ()
{
  char *ret;
  time_t t;
  struct tm *ti;
  time (&t);
  if ((ti = localtime (&t)) == NULL)
    return (char *) "N/A";
  if ((ret = asctime (ti)) == NULL)
    return (char *) "N/A";

  ret[strlen (ret) - 1] = '\0';
  return ret;

}


/* Write messages to stdout, stderr, syslog and log file (if applicable and as appropriate). */
int
log_msg (int level, char *fmt, ...)
{

  va_list arglist;
  char *str;
  va_start (arglist, fmt);
  if (vasprintf (&str, fmt, arglist) < 0){
    fprintf(stderr, "Unable to format log string");
    return FALSE;
  }

  char *time_str = NULL;
  Sasprintf (time_str, "%s - %s", get_current_time (), str);

#ifdef ULTRA_DEBUG
  fprintf(stdout, "%s\n", time_str); 

#else  
  /* Print out *everything* when debug (-b) is enabled. */
  if (cfg->debug && level < LOG_MEMDEBUG)
    fprintf (stdout, "%s\n", time_str);

  /* Less severe than a warning (LOG_NOTICE and LOG_INFO) goes on stdout */
  else if (cfg->verbose && level > LOG_WARNING && level < LOG_DEBUG)
    fprintf (stdout, "%s\n", time_str);

  /* Whether we're verbose or not, we need to know about warnings and worse */
  if (!cfg->debug && level <= LOG_WARNING)
    fprintf (stderr, "%s\n", time_str);

#endif

  if (cfg->log_file != NULL && log_fp != NULL && level <= LOG_DEBUG) {
    if (fprintf (log_fp, "%s\n", time_str) < 0) {
      log_msg (LOG_ERR, "Failed writing to log file. Disabling logging...\n");
      log_fp = NULL;
    }
    fflush (log_fp);
  }

  /* Don't spam syslog with debugging stuff. Only log useful things. Also strip out any newlines we use for cosmetic
   * purposes on std(out|err) 
   */
  strip_char (str, '\n');
  strip_char (str, '\t');
  if (cfg->syslog && level <= LOG_WARNING) {
    syslog (level, "%s", str);
  }

  va_end (arglist);
  free(str);
  free(time_str);
  return 0;
}
