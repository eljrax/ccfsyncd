#include "ccfsync.h"

/* Throw away whatever's in the queue - called when 
 * we need to exit, so to put our sentinels at the front 
 * of the queue. 
 * Should be quicker than sorting queue and try to get sentinel to the front 
 */
void
drain_queue (GAsyncQueue * queue)
{

  int i;
  int queue_len = (int) g_async_queue_length (queue);
  if (queue_len < 1)
    return;
  g_async_queue_lock (queue);
  for (i = 0; i < queue_len; i++) {
    g_async_queue_pop_unlocked (queue);
  }
  g_async_queue_unlock (queue);

}


void
validate_error (char *variable)
{
  fprintf (stderr, "Cannot proceed without %s. Please review the output of --help\n", variable);
  exit (EXIT_FAILURE);
}

int
is_dir (char *filename)
{
  int ret;
  struct stat st;
  ret = stat (cfg->monitor_dir, &st);
  if (ret < 0)
    log_msg (LOG_CRIT, "Failed to stat() file %s: %s", cfg->monitor_dir, strerror (errno));
  if (S_ISDIR (st.st_mode))
    return 1;
  else
    return 0;
}

void
validate_config ()
{
  if (cfg->container == NULL)
    validate_error ("a valid container (-c)");
  if (cfg->monitor_dir == NULL || !is_dir (cfg->monitor_dir))
    validate_error ("a valid directory to watch (-d)");
  if (cfg->api_username == NULL)
    validate_error ("a valid API username (-u)");
  if (cfg->api_key == NULL)
    validate_error ("A valid API key (-k)");
  if (cfg->pid_file == NULL)
    validate_error ("A PID file path (-p)");
  if (cfg->num_upload_threads + cfg->num_delete_threads + cfg->num_copy_threads > 100 )
    log_msg(LOG_WARNING, "Warning: Number of threads exceed 100. This is counter-productive, as CF will throttle you. Please consider lowering your thread count");


  if (cfg->verbose)
    log_msg (LOG_INFO, "Config validation succeeded. We can proceed...\n");

  return;

}

void
help (char *binary_name)
{

  printf ("Usage: %s [OPTION]\n \
Monitors local directory, keeping a Rackspace Cloud Files container in sync\n\n \
\
%s will look for a configuration file in %s\n\n \
  -h, --help\t\tThis help text\n \
  -f, --config-file\tConfiguration file to parse and use\n \
  -c, --container\tContainer to sync files to\n \
  -d, --local-dir\tLocal directory to sync to CF\n \
  -t, --threads\tNumber of threads of each type to use (upload, copy, delete) (default: 5)\n \
  -v, --verbose\tVerbose output or logging\n \
  -g, --foreground\tStay in foreground, for debugging purposes\n \
  -a, --auth-endpoint\tURL to use for authentication (default should work)\n \
  -u, --username\tCloud username to use for authentication\n \
  -k, --api-key\tAPI key to use for authentication\n \
  -r, --region\t\tRegion to use for authentication (default, empty, should work)\n \
  -l, --log-file\tLog file to log to (has no effect it syslog logging is enabled)\n \
  -n, --no-syslog\tDo not log through the system log facility\n \
  -s, --no-service-net\tDo not use servicenet, use public network to talk to CF\n \
  -e, --exclusion-file\tFile containing a list of regexes for files we shouldn't sync\n \
  -p, --pid-file\tPID file to write to (default: %s)\n \
  -q, --quit\t\tKill a running %s process\n \
  -b, --debug\t\tEnable debug output (this is pretty noisy)\n \
\n", binary_name, PACKAGE_NAME, cfg->config_file, cfg->pid_file, PACKAGE_NAME);

  exit (EXIT_FAILURE);
}

int
char_to_pos_int (char *str)
{
  char *ptr = NULL;
  int ret = strtol (str, &ptr, 10);
  if (*ptr) {
    log_msg (LOG_DEBUG, "Conversion failure: '%s'\n", ptr);
    return -1;
  }
  return ret;

}

void
suicide (gchar * fmt, ...)
{
  va_list arglist;
  char *msg;
  if (fmt) {
    va_start (arglist, fmt);
    if (vasprintf (&msg, fmt, arglist) < 0)
      log_msg(LOG_CRIT, "Fatal error occurred! (additionally generating the error message failed. Memory issues?");
    else
      log_msg (LOG_CRIT, msg);
  }
  
  if (!threaded)
    exit(EXIT_FAILURE);

  /* This will run through signal handlers and exit as nicely as possible */
  kill (getpid (), SIGTERM);
}


/* Returns a GList of all remote files beginning with /arbitrary/dir */
GList *
get_cf_files_from_dir (gchar * dir, struct exclusions *exclusions)
{
  GList *files = NULL;
  GHashTable *files_hash = g_hash_table_new (g_str_hash, g_str_equal);
  list_files_cf (&files_hash, NULL, exclusions);
  GHashTableIter iter;
  gpointer key, value;

  g_hash_table_iter_init (&iter, files_hash);
  while (g_hash_table_iter_next (&iter, &key, &value)) {
    if (strncmp (key, dir, strlen ((char *) dir)) == 0) {
      files = g_list_prepend (files, g_strdup (key));
    }
    destroy_cf_file ((cf_file *) value, key);
  }

  g_hash_table_destroy (files_hash);
  return files;

}

GList *
get_dirs (gchar * name, gchar * parent)
{

  DIR *dir;
  struct dirent *entry;
  unsigned int i;
  GList *ret = NULL;
  if (!(dir = opendir (name))) {
    perror ("opendir:");
    return NULL;
  }
  if (!(entry = readdir (dir))) {
    perror ("readdir:");
    return NULL;
  }

  do {
    if (entry->d_type == DT_DIR) {
      if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0)
	continue;

      char path[PATH_MAX + 1];
      int len = snprintf (path, sizeof (path), "%s/%s", name, entry->d_name);
      path[len] = '\0';
      ret = g_list_append (ret, g_strdup (path));

      /* Recurse and add to our return GList */
      GList *tmp = get_dirs (path, parent);
      for (i = 0; i < g_list_length (tmp); i++) {
	gchar *tmp_data = g_strdup (g_list_nth_data (tmp, i));
	ret = g_list_prepend (ret, g_strdup (tmp_data));
	free_single_pointer (tmp_data);
      }
      g_list_free_full (tmp, free_single_pointer);

    }
  } while ((entry = readdir (dir)));

  closedir (dir);

  return ret;
}

/* Needed when we're deleting files on remote - this is 
 * basically building a mock object where only the name is 
 * essential 
 */
cf_file *
build_cf_file_from_lf (gchar * name)
{
  cf_file *cf = malloc (sizeof (cf_file));
  cf->name = g_strdup (name);
  cf->sentinel = g_strdup ("ok");
  cf->content_type = g_strdup ("dummy");
  cf->hash = g_strdup ("dummy");
  cf->local_path = g_strdup ("dummy");

  return cf;
}

void
strip_char (char *str, char c)
{
  char *ro_ptr = str, *ret = str;
  while (*ro_ptr) {
    *ret = *ro_ptr++;
    ret += (*ret != c);
  }
  *ret = '\0';
}
