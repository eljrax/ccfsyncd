#include "ccfsync.h"
#include <pthread.h>
#include <openssl/err.h>

/* Blocks on popping the queue containing files to do server-side copy 
 * takes a struct thread_data containing the auth struct and container name
*/
// $ curl -X COPY -H "Destination: images/rackspace.jpeg" -H "X-Auth-Token: 3c5c817e2b" https://storage101.ord1.clouddrive.com/v1/MossoCloudFS_c4f83243-7537-4600-a94d-ab7065f0a27b/images/rackspace.jpg

int
do_copy (cf_file_copy * cfc, gchar * token_header, gchar * cf_url, gchar * dest_header, int thid)
{
  CURL *curl;
  CURLcode res;
  long http_code = 0;
  struct curl_slist *headerlist = NULL;

  log_msg (LOG_DEBUG, "Copy thread %d: Copying '%s'   to:  '%s'", thid, cfc->old_name, cfc->new_name);
  log_msg (LOG_DEBUG, "Copy thread %d: Sending auth header: %s", thid, token_header);
  log_msg (LOG_DEBUG, "Copy thread %d: Sending dest header: %s", thid, dest_header);

  if ((curl = curl_easy_init ()) == NULL) {
    log_msg (LOG_ERR, "Copy thread %d: Failed to initialise curl!", thid);
    return -1;
  }

  headerlist = curl_slist_append (headerlist, token_header);
  headerlist = curl_slist_append (headerlist, dest_header);
  curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headerlist);
  curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "COPY");
  curl_easy_setopt (curl, CURLOPT_URL, cf_url);
  if (!cfg->debug)
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_devnull);
  res = curl_easy_perform (curl);
  curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
  if (res != CURLE_OK)
    log_msg (LOG_CRIT, "Copy thread %d: curl_easy_perform() failed: %s attempting to continue. Please investigate!", thid, curl_easy_strerror (res));

  curl_slist_free_all (headerlist);
  curl_easy_cleanup (curl);

  /* This is to work around a memory-leak in curl */
  ERR_remove_thread_state (NULL);


  return http_code;
}

void *
copy_file_and_remove (void *data)
{

  thread_data *thd = (thread_data *) data;

  log_msg (LOG_DEBUG, "Copy thread: %d spawned", thd->thread_id);

  while (1) {

    int http_code = 0;
    int retries = 5;
    int was_copied = FALSE;

    cf_file_copy *cfc = g_async_queue_pop (files_to_copy);

    /* A sentinel-event is put on the queue when we're asked to exit. Kill the thread when we hit one of those */
    if (cfc->sentinel != NULL) {
      if (strncmp (cfc->sentinel, "exit", strlen ("exit")) == 0) {
	log_msg (LOG_DEBUG, "Copy thread %d asked to exit...", thd->thread_id);
	destroy_cf_file_copy (cfc);
	free_single_pointer (thd);
	pthread_exit (EXIT_SUCCESS);
      }
    }

    char *cf_url = NULL;
    char *token_header = NULL;
    char *dest_header = NULL;

    do {
      Sasprintf (cf_url, "%s/%s/%s", auth->endpoint, cfg->container, cfc->old_name);
      Sasprintf (dest_header, "%s%s/%s", "Destination: ", cfg->container, cfc->new_name);


      log_msg (LOG_DEBUG, "\n\nCopy thread %d: --- File copy ---", thd->thread_id);
      log_msg (LOG_DEBUG, "Copy thread %d: Source: %s", thd->thread_id, cfc->old_name);
      log_msg (LOG_DEBUG, "Copy thread %d: Destination: %s/%s", thd->thread_id, cfg->container, cfc->cf_file->name);
      log_msg (LOG_DEBUG, "Copy thread %d: Sending auth header: %s", thd->thread_id, auth->token_header);
      log_msg (LOG_DEBUG, "Copy thread %d: Using url: %s", thd->thread_id, cf_url);

      http_code = do_copy (cfc, auth->token_header, cf_url, dest_header, thd->thread_id);
      log_msg (LOG_DEBUG, "Copy thread %d: HTTP return code: %d", thd->thread_id, http_code);

      if (http_code == 201) {
	was_copied = TRUE;
        free_single_pointer (dest_header);
        free_single_pointer (cf_url);
	break;
      }
      else if (http_code == 401) {
	log_msg (LOG_INFO, "Copy thread %d: Authentication error - token expired? Reauthenticating\n", thd->thread_id);

	if (pthread_mutex_trylock (&auth_in_progress_mutex)) {
	  doAuth (REAUTH);
	  pthread_mutex_unlock (&auth_in_progress_mutex);
	  log_msg (LOG_DEBUG, "Copy thread %d: Got new token: '%s'", thd->thread_id, auth->token);
	}
	else {
	  log_msg (LOG_DEBUG, "Copy thread %d: Another thread is authenticating - sleeping 1 second and re-trying copy", thd->thread_id);
	}
      }
      else {
	log_msg (LOG_DEBUG, "Copy thread %d: Unhandled HTTP return code in file copy: %d file: %s", thd->thread_id, http_code, cfc->cf_file->name);
      }

      free_single_pointer (cf_url);
      cf_url = NULL;
      free_single_pointer (dest_header);
      dest_header = NULL;
      sleep (1);

    } while (retries-- > 0);

    if (!was_copied)
      log_msg (LOG_ERR, "Copy thread %d: CF file '%s' failed to be copied to '%s'! HTTP return code: %d ", thd->thread_id, cfc->old_name, cfc->new_name, http_code);
    else
      log_msg (LOG_DEBUG, "Copy thread: %d: Rename of file '%s' to '%s' successful", thd->thread_id, cfc->old_name, cfc->new_name);

    g_async_queue_push (files_to_delete, cfc->cf_file);
    destroy_cf_file_copy (cfc);
  }
}
