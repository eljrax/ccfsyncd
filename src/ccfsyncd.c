#define _XOPEN_SOURCE
#include "ccfsync.h"

GAsyncQueue *files_to_upload;
GAsyncQueue *files_to_delete;
GAsyncQueue *files_to_copy;
pthread_mutex_t watches_mutex;
pthread_mutex_t move_events_mutex;
pthread_mutex_t auth_in_progress_mutex;

pthread_mutex_t files_being_uploaded_mutex;
GList *files_being_uploaded;
GList *move_events;

struct auth *auth;
FILE *log_fp;
config *cfg;
/* Indicates whether we're already in the process of exiting */
int exiting;
/* Indicates whether we're threaded yet */
int threaded;


/* Does the actual request for getting the files, modifies 'ret' to contain the json returned. 
 * Returns the HTTP return code from CF API
 */
int
get_cf_files_list (gchar * marker, struct string **resp)
{


  CURL *curl;
  CURLcode res;
  struct curl_slist *headerlist = NULL;
  char *token_header = NULL;
  long http_code = 0;
  gchar *cf_url = NULL;

  /* If marker is NULL, we want all files from the beginning, we're not recursing */
  if (marker == NULL) {
    Sasprintf (cf_url, "%s/%s", auth->endpoint, cfg->container);
  }
  else {
    Sasprintf (cf_url, "%s/%s%s%s", auth->endpoint, cfg->container, "?marker=", marker);
  }

  Sasprintf (token_header, "X-Auth-Token: %s", auth->token);

  init_string (*resp);
  curl = curl_easy_init ();
//  curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1L);

  if (!curl) {
    log_msg (LOG_ERR, "Failed to init curl: %s\n", strerror (errno));
    return -1;
  }


  headerlist = curl_slist_append (headerlist, (const char *) "Accept: application/json");
  headerlist = curl_slist_append (headerlist, token_header);

  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt (curl, CURLOPT_URL, cf_url);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt (curl, CURLOPT_WRITEDATA, *resp);

  res = curl_easy_perform (curl);
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (res != CURLE_OK) {
    log_msg (LOG_ERR, "Error performing request: %s\n", curl_easy_strerror (res));
    http_code = -1;
  }

  curl_slist_free_all (headerlist);
  curl_easy_cleanup (curl);
  free_single_pointer (token_header);
  g_free (cf_url);

  return http_code;
}

void
list_files_cf (GHashTable ** cf_files, gchar * marker, struct exclusions *exclusions)
{

  gchar *ret;
  gchar *last_file;
  struct string *resp = malloc (sizeof (struct string));
  int http_code = 0;
  int retries = 5;

  do {

    http_code = get_cf_files_list (marker, &resp);
    if (http_code == 200)
      retries = 0;
    else if (http_code == 401) {
      log_msg (LOG_DEBUG, "list_files_cf: Authentication error - reauthenticating\n");
      if (pthread_mutex_trylock (&auth_in_progress_mutex)) {
	doAuth (REAUTH);
	pthread_mutex_unlock (&auth_in_progress_mutex);
	log_msg (LOG_DEBUG, "list_files_cf: Got new token! New token: '%s'\n", auth->token);
      }
      /* Another thread has the auth_in_progress mutex, so we'll wait a second (but max 5 times) for a new token */
      else {
	sleep (1);
      }
    }
    else {
      sleep (1);
      log_msg (LOG_WARNING, "Got %d back when trying to list files. Container doesn't exist? Retrying (retries remaining: %d)...", http_code, retries);
    }
  }
  while (retries-- > 0);

  if (http_code != 200)
    suicide ("Something went wrong when listing files in container %s. Bailing...\n", cfg->container);

  ret = g_strdup (resp->data);

  free_single_pointer (resp->data);
  free_single_pointer (resp);


  json_t *root, *obj, *val;
  json_error_t error;

  root = json_loads (ret, 0, &error);

  if (!root)
    suicide ("Failed to load root of JSON response when obtaining list of remote files\n");

  if (!json_is_array (root)) {
    json_decref (root);
    return;
  }

  unsigned int i;
  for (i = 0; i < json_array_size (root); i++) {

    cf_file *f = g_malloc (sizeof (cf_file));
    obj = json_array_get (root, i);
    val = json_object_get (obj, "name");
    f->name = g_strdup (json_string_value (val));

    /* Don't do anything with files we're explicitly excluding */
    if (regex_match(f->name, exclusions)){
      free_single_pointer(f->name);
      free_single_pointer(f);
      continue;
    }
    log_msg (LOG_DEBUG, "Remote file found: %s", f->name);

    val = json_object_get (obj, "bytes");
    f->len = json_integer_value (val);

    val = json_object_get (obj, "content_type");
    f->content_type = g_strdup (json_string_value (val));

    val = json_object_get (obj, "last_modified");
    gchar *tmp = g_strdup (json_string_value (val));
    struct tm tm = { 0 };

    if (strptime (tmp, "%Y-%m-%dT%T", &tm) == NULL) {
      log_msg (LOG_WARNING, "Last modified date converstion problem?");
      return;
    }

    f->last_modified = mktime (&tm);

    val = json_object_get (obj, "hash");
    f->hash = g_strdup (json_string_value (val));


    char *local_path = NULL;
    Sasprintf (local_path, "%s/%s", cfg->monitor_dir, f->name);
    f->local_path = g_strdup (local_path);
    f->sentinel = g_strdup ("ok");

    free_single_pointer (local_path);

    g_hash_table_insert (*cf_files, f->name, f);
    if (i == json_array_size (root) - 1)
      last_file = f->name;
    g_free (tmp);
  }
  if (g_hash_table_size (*cf_files) == 10000) {
    /* Get the next 10000 files */
    list_files_cf (cf_files, last_file, exclusions);

  }
  json_decref (root);
  free_single_pointer (ret);

}

int
main (int argc, char *argv[])
{

#if NEED_INIT_THREADS == 1
  g_thread_init (NULL);
#endif

  threaded = FALSE;
  GHashTable *cf_files = g_hash_table_new (g_str_hash, g_str_equal);
  GHashTable *local_files = NULL;
  struct exclusions *exclusions;

  init_config (argc, argv);
  init_logging ();

  exclusions = init_exclusions ();
  /* Daemonise (unless told not to) */
  daemonise ();
  
  exiting = FALSE;

  log_msg (LOG_INFO, "%s starting", PACKAGE_NAME);

  signal (SIGINT, signal_handler);
  signal (SIGTERM, signal_handler);
  signal (SIGQUIT, signal_handler);

  files_to_upload = g_async_queue_new_full ((GDestroyNotify) destroy_local_file);
  files_to_delete = g_async_queue_new_full ((GDestroyNotify) destroy_cf_file);
  files_to_copy = g_async_queue_new_full ((GDestroyNotify) destroy_cf_file_copy);
  pthread_mutex_init (&auth_in_progress_mutex, NULL);
  pthread_mutex_init (&files_being_uploaded_mutex, NULL);
  pthread_mutex_init (&watches_mutex, NULL);
  pthread_mutex_init (&move_events_mutex, NULL);
  files_being_uploaded = NULL;
  move_events = NULL;

  if (curl_global_init (CURL_GLOBAL_DEFAULT) != 0) {
    suicide("Failed to initialise curl: %s", strerror (errno));
  }

  /* doauth.c - authenticates and populates the global auth struct */
  init_auth ();

  list_files_cf (&cf_files, NULL, exclusions);

  local_files = list_files_local (cfg->monitor_dir, cfg->monitor_dir, exclusions);
  if (local_files == NULL) {
    log_msg (LOG_CRIT, "Failed to obtain list of local files.\n");
    exit (EXIT_FAILURE);
  }

  threaded = TRUE;
  struct thread_inventory *thread_inventory = spawn_threads ();

  /* Thread monitoring the filesystem for changes and populating appropriate queues */
  int rc;
  pthread_t monitor_dir_thread;
  pthread_attr_t attr;
  pthread_attr_init (&attr);
  pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
  rc = pthread_create (&monitor_dir_thread, &attr, monitor_dir_inotify, (void *) exclusions);
  if (rc != 0)
    suicide ("Failed to spawn filesystem monitor thread: %s Bailing...", strerror (errno));


  /* populate GQueue files_to_upload */
  GList *to_be_free_lf = compare_remote (local_files, cf_files);
  /* populate GQueue files_to_delete */
  compare_local (cf_files, local_files);

  /* Now remove any lf structures we no longer need (ie. they are already synced don't need to be uploaded) */
  free_lfs (to_be_free_lf, local_files);
  g_list_free_full (to_be_free_lf, free_single_pointer);
  g_hash_table_destroy (cf_files);
  g_hash_table_destroy (local_files);


  /* We'll block here until we're asked to quit */
  wait_threads (thread_inventory);
  log_msg (LOG_DEBUG, "All remote action threads have exited. Killing inotify thread...");

  /* the inotify thread is blocking on read(), so we can't ask it nicely to quit without messing with the FS */
  signal (SIGINT, signal_ignore);
  signal (SIGTERM, signal_ignore);
  signal (SIGQUIT, signal_ignore);
  pthread_kill (monitor_dir_thread, SIGTERM);

  delete_local_file (cfg->pid_file);
  cleanup_globals ();
  destroy_exclusions (exclusions);
  free_single_pointer (thread_inventory);

  log_msg (LOG_INFO, "%s exiting\n", PACKAGE_NAME);

  destroy_logging ();
  return 0;
}
