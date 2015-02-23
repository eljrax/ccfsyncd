#include "ccfsync.h"
#include <pthread.h>
#include <openssl/err.h>

int
do_delete (gchar * token_header, gchar * cf_url, int thid)
{

  CURL *curl;
  CURLcode res;
  struct curl_slist *headerlist = NULL;
  long http_code = 0;

  if ((curl = curl_easy_init ()) == NULL) {
    log_msg (LOG_ERR, "Delete thread %d: Failed to initialise curl!", thid);
    return -1;
  }

  headerlist = curl_slist_append (headerlist, token_header);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "DELETE");
  curl_easy_setopt (curl, CURLOPT_URL, cf_url);
  if (!cfg->debug)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_devnull);

  res = curl_easy_perform (curl);
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (res != CURLE_OK)
    log_msg (LOG_ERR, "Delete thread %d: Request failed: %s", thid, curl_easy_strerror (res));
  curl_easy_cleanup (curl);

  curl_slist_free_all (headerlist);
  /* This is to work around a memory-leak in curl */
  ERR_remove_thread_state (NULL);
  return http_code;
}

/* Blocks on popping the queue containing files to delete 
 * takes a struct thread_data containing the auth struct and container name
*/
void *
delete_file (void *data)
{

  thread_data *thd = (thread_data *) data;

  log_msg (LOG_DEBUG, "Delete thread: %d spawned", thd->thread_id);

  int retries = 5;
  int was_deleted = FALSE;

  while (1) {
    int http_code = 0;
    cf_file *cf = g_async_queue_pop (files_to_delete);

    /* A sentinel-event is put on the queue when we're asked to exit. Kill the thread when we hit one of those */
    if (cf->sentinel != NULL) {
      if (strncmp (cf->sentinel, "exit", strlen ("exit")) == 0) {
	log_msg (LOG_DEBUG, "Delete thread %d: asked to exit...", thd->thread_id);
	destroy_cf_file (cf, NULL);
	free_single_pointer (thd);
	pthread_exit (EXIT_SUCCESS);
      }
    }

    gchar *cf_url = NULL;
    gchar *token_header = NULL;

    do {
      Sasprintf (cf_url, "%s/%s/%s", auth->endpoint, cfg->container, cf->name);

      log_msg (LOG_DEBUG, "\n\nDelete thread %d: Deleting '%s'", thd->thread_id, cf->name);
      log_msg (LOG_DEBUG, "Delete thread %d: Sending auth header: %s", thd->thread_id, auth->token_header);
      log_msg (LOG_DEBUG, "Delete thread %d: Using url: %s", thd->thread_id, cf_url);
      log_msg (LOG_DEBUG, "Delete thread %d: Local file is '%s'", thd->thread_id, cf->local_path);

      http_code = do_delete (auth->token_header, cf_url, thd->thread_id);
      log_msg (LOG_DEBUG, "Delete thread %d: HTTP return code: %d", thd->thread_id, http_code);

      if (http_code == 204) {
	was_deleted = TRUE;
        free_single_pointer (cf_url);
	break;
      }
      else if (http_code == 401) {
	log_msg (LOG_INFO, "Delete thread %d: Authentication error - token expired? Reauthenticating\n", thd->thread_id);

	if (pthread_mutex_trylock (&auth_in_progress_mutex)) {
	  doAuth (REAUTH);
	  pthread_mutex_unlock (&auth_in_progress_mutex);
	  log_msg (LOG_DEBUG, "Delete thread %d: Got new token: '%s'", thd->thread_id, auth->token);
	}
	else {
	  log_msg (LOG_DEBUG, "Delete thread %d: Another thread is authenticating - sleeping 1 second and re-trying delete", thd->thread_id);
	}
      }
      else {
	log_msg (LOG_DEBUG, "Delete thread %d: Unhandled HTTP return code in file delete: %d file: %s\n", thd->thread_id, http_code, cf->name);
      }

      /* Need to prepare these for Sasprintf() again */
      free_single_pointer (cf_url);
      cf_url = NULL;
      sleep (1);

    } while (retries-- > 0);

    if (!was_deleted)
      log_msg (LOG_ERR, "Delete thread %d: WARNING: File '%s' failed to delete off CF! HTTP return code: %d", thd->thread_id, cf->name, http_code);
    else
      log_msg (LOG_DEBUG, "Delete thread %d: Deletion of '%s' successful", thd->thread_id, cf->name);

    destroy_cf_file (cf, cf->name);
  }
}
